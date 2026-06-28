// oran_analyze.ls — a small "tshark-like" CLI built on oran.cus.
//
// Usage:
//   oran_analyze <file>                 binary capture (back-to-back eCPRI frames)
//   oran_analyze <file> --hex           file is a continuous hex string
//   oran_analyze <file> --hexdump       file is tcpdump -x/-X output
//   ...filters:   --section-type N      keep only Section Type N
//                 --eaxc N              keep only eAxC N (decimal)
//   ...outputs:   --count               print packet count
//                 --avg-sinr            print mean SINR over the (filtered) set
//                 --to json|text|html   render the (filtered) packets
//
// With no input file it runs a built-in demo capture as a self-test and prints
// "ORAN ANALYZE PASS" (so it doubles as a ctest sample).
import std.sys.c as c
import std.sys.proc as proc
import std.sys.io as io
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }
def chkf(f64 got, f64 want, Str msg) {
    f64 d = got - want
    if d < 0.0 { d = 0.0 - d }
    if d > 0.001 { @print(msg); @print(got); c.abort() }
}

// Build a synthetic capture: ST1 DL control + ST9(eAxC 0x0103: 18,22 dB)
//                                              + ST9(eAxC 0x0104: 10,14 dB)
def build_demo() -> Str {
    Writer w = oran.writer()

    CplaneSt1 ctrl = {}
    ctrl.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2, payload_size: 20, rtcid_pcid: 0x0102, seqid: 1 }
    ctrl.hdr = CpCommonHdr{ dir: true, payload_ver: 1, filter_idx: 0, frame_id: 11,
        subframe_id: 0, slot_id: 1, start_symbol_id: 0, num_sections: 1, section_type: 1 }
    ctrl.ud_comp_hdr = 0
    ctrl.sections.push(SecType1{ section_id: 1, rb: false, sym_inc: false,
        start_prbc: 0, num_prbc: 273 & 0xFF, re_mask: 0xFFF, num_symbol: 14, ef: false, beam_id: 7 })
    ctrl.write(&!w)

    demo_st9(&!w, 0x0103, 18, 22)
    demo_st9(&!w, 0x0104, 10, 14)
    return w.take()
}

def demo_st9(&!Writer w, int eaxc, int v0, int v1) {
    CplaneSt9 m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2, payload_size: 14, rtcid_pcid: eaxc, seqid: 1 }
    m.hdr = CpCommonHdr{ dir: false, payload_ver: 1, filter_idx: 0, frame_id: 11,
        subframe_id: 0, slot_id: 0, start_symbol_id: 3, num_sections: 1, section_type: 9 }
    m.num_sinr_per_prb = 1  m.oru_ctrl_slot_mask_id = 1  m.iq_width = 8  m.comp_meth = 0
    SecType9 s = {}
    s.section_id = 1  s.start_prbu = 0  s.num_prbu = 2
    SinrPrb p0 = {}  p0.sinr_raw.push(v0)
    SinrPrb p1 = {}  p1.sinr_raw.push(v1)
    s.prbs.push(p0)  s.prbs.push(p1)
    m.sections.push(s)
    m.write(&!w)
}

def run_demo() {
    Str bytes = build_demo()
    Reader r = b.of_bytes(bytes)
    Vec(Packet) pkts = oran.parse_all(&!r, 8, 0)

    @print("=== oran_analyze demo capture ===")
    @print(oran.to_text_all(pkts))

    chk(pkts.len() == 3, "demo: 3 packets")

    Vec(Packet) sinr = pkts.filter(|p| oran.section_type_of(p) == 9)
    chk(sinr.len() == 2, "demo: 2 ST9")
    chkf(oran.mean_sinr(sinr), 16.0, "demo: overall mean SINR 16")

    Vec(Packet) ue = pkts.filter(|p| oran.eaxc_of(p) == 0x0103)
    chkf(oran.mean_sinr(ue), 20.0, "demo: UE 0x0103 mean SINR 20")

    @print("mean SINR (all ST9): 16.0  |  UE 0x0103: 20.0")
    @print("ORAN ANALYZE PASS")
}

def make_reader(Str infile, bool hexmode, bool hexdump) -> Reader {
    Str content = io.read_file(infile).unwrap_or("")
    if hexdump { return oran.from_hexdump(content) }
    if hexmode { return oran.from_hex(content) }
    return b.of_bytes(content)
}

def main() {
    Vec(Str) args = proc.args()
    Str infile = ""
    bool hexmode = false
    bool hexdump = false
    int fST = -1
    int fEaxc = -1
    bool doCount = false
    bool doAvg = false
    Str fmt = ""

    int i = 0
    int n = args.len()
    while i < n {
        Str a = args[i]
        if a.eq?("--hex") { hexmode = true }
        else if a.eq?("--hexdump") { hexdump = true }
        else if a.eq?("--count") { doCount = true }
        else if a.eq?("--avg-sinr") { doAvg = true }
        else if a.eq?("--section-type") { if i + 1 < n { i = i + 1; fST = args[i].to_int().unwrap_or(-1) } }
        else if a.eq?("--eaxc") { if i + 1 < n { i = i + 1; fEaxc = args[i].to_int().unwrap_or(-1) } }
        else if a.eq?("--to") { if i + 1 < n { i = i + 1; fmt = args[i] } }
        else if !a.contains?("--") { infile = a }
        i = i + 1
    }

    // no input file -> built-in demo self-test
    if infile.len() == 0 { run_demo(); return }

    Reader r = make_reader(infile, hexmode, hexdump)
    Vec(Packet) pkts = oran.parse_all(&!r, 8, 0)
    Vec(Packet) sel = pkts.filter(|p|
        (fST < 0 || oran.section_type_of(p) == fST) && (fEaxc < 0 || oran.eaxc_of(p) == fEaxc))

    if doCount { @print(f"count={sel.len()}") }
    if doAvg { @print(f"mean_sinr={oran.mean_sinr(sel)}") }
    if fmt.eq?("json") { @print(oran.to_json_all(sel)) }
    else if fmt.eq?("text") { @print(oran.to_text_all(sel)) }
    else if fmt.eq?("html") { @print(oran.to_html_all(sel)) }
}
