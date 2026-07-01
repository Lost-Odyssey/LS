// oran_o1o2_test.ls — O1/O2 output formats: hex dump (xxd + field-annotated)
// and CSV dump. Builds a 3-packet capture (ST1 DL ctrl + ST9 SINR + U-plane IQ)
// and checks the rendered text for the key offsets/fields/rows.
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }
def has(&Str hay, Str needle, Str msg) { if !hay.contains?(needle) { @print(msg); @print(hay); c.abort() } }

def st1ctrl() -> CplaneSt1 {
    CplaneSt1 m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2, payload_size: 20, rtcid_pcid: 0x0102, seqid: 1 }
    m.hdr = CpCommonHdr{ dir: true, payload_ver: 1, filter_idx: 0, frame_id: 50,
        subframe_id: 0, slot_id: 1, start_symbol_id: 0, num_sections: 1, section_type: 1 }
    m.ud_comp_hdr = 0
    // num_prbc is an 8-bit field (0..255); 0 means "all carrier PRBs". Use 100 for a
    // concrete partial allocation (NR 100 MHz full-band would use 0=all instead).
    m.sections.push(SecType1{ section_id: 9, rb: false, sym_inc: false,
        start_prbc: 0, num_prbc: 100, re_mask: 0xFFF, num_symbol: 14, ef: false, beam_id: 4 })
    return m
}

def st9sinr(int eaxc) -> CplaneSt9 {
    CplaneSt9 m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2, payload_size: 14, rtcid_pcid: eaxc, seqid: 1 }
    m.hdr = CpCommonHdr{ dir: false, payload_ver: 1, filter_idx: 0, frame_id: 50,
        subframe_id: 0, slot_id: 0, start_symbol_id: 3, num_sections: 1, section_type: 9 }
    m.num_sinr_per_prb = 1  m.oru_ctrl_slot_mask_id = 1  m.iq_width = 8  m.comp_meth = 0
    SecType9 s = {}
    s.section_id = 1  s.start_prbu = 0  s.num_prbu = 2
    SinrPrb p0 = {}  p0.sinr_raw.push(18)
    SinrPrb p1 = {}  p1.sinr_raw.push(22)
    s.prbs.push(p0)  s.prbs.push(p1)
    m.sections.push(s)
    return m
}

def make_uplane(int eaxc) -> UplaneMsg {
    UplaneMsg m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 0, payload_size: 0, rtcid_pcid: eaxc, seqid: 7 }
    m.dir = true  m.payload_ver = 1  m.filter_idx = 0
    m.frame_id = 50  m.subframe_id = 2  m.slot_id = 1  m.symbol_id = 5
    m.static_comp = false  m.iq_width = 16  m.comp_meth = 0
    UplaneSection s = {}
    s.section_id = 1  s.start_prbu = 0  s.num_prbu = 2
    for pi in 0..2 {
        UplanePrb prb = {}
        for re in 0..12 { int v = pi * 10 + re + 1  prb.i_samples.push(v)  prb.q_samples.push(0 - v) }
        s.prbs.push(prb)
    }
    m.sections.push(s)
    return m
}

def main() {
    Writer w = oran.writer()
    st1ctrl().write(&!w)
    st9sinr(0x0103).write(&!w)
    make_uplane(0x0105).write(&!w)
    Reader r = w.as_reader()
    Vec(Packet) pkts = oran.parse_all(&!r, 8, 0)
    chk(pkts.len() == 3, "3 packets")

    // ---- O1a: xxd-style hex dump ----
    Str hx = oran.to_hex_all(pkts)
    has(hx, "--- packet[0] ---", "hex has packet 0 banner")
    has(hx, "--- packet[2] ---", "hex has packet 2 banner")
    has(hx, "0x0000  10 02 00 14", "hex first row offset+bytes")  // ecpri ver=1, msg=2, payload=20
    has(hx, "0x0010 ", "hex second row offset")

    // single-packet hex
    Str hx0 = oran.to_hex(pkts[0])
    has(hx0, "10 02 00 14 01 02 00 01", "single ST1 ecpri bytes")

    // ---- O1b: field-annotated dump ----
    Str an = oran.to_annotated_all(pkts)
    has(an, "eCPRI:", "annot eCPRI line")
    has(an, "+8B", "annot eCPRI is 8 bytes")
    has(an, "eAxC=0x0102", "annot ST1 eAxC")
    has(an, "sectionType=1", "annot ST1 sectionType")
    has(an, "numPrbc=100", "annot ST1 numPrbc")
    has(an, "beamId=4", "annot ST1 beamId")
    has(an, "sectionType=9", "annot ST9 sectionType")
    has(an, "meanSINR=20", "annot ST9 mean SINR")
    has(an, "u-section[0]:", "annot U-plane section")
    has(an, "compMeth=0", "annot U-plane compMeth")

    // ---- O2: CSV dump ----
    Str csv = oran.to_csv(pkts)
    has(csv, "type,eaxc,dir,frameId", "csv header")
    has(csv, "ST1,258,DL,50,0,1,0,1,9,0,100,4,,,", "csv ST1 row")
    has(csv, "ST9,259,UL,50,0,0,3,9,1,0,2,,20", "csv ST9 row with mean")
    has(csv, "U-IQ,261,DL,50,2,1,5,,1,0,2,,,16,0", "csv U-IQ row")

    // CSV is line-oriented: header + one row per section = 1 + 3 lines (+ trailing)
    Vec(Str) lines = csv.split("\n")
    chk(lines.len() >= 4, "csv has header + 3 rows")

    @print("ORAN O1O2 PASS")
}
