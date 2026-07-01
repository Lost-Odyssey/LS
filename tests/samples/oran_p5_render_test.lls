// oran_p5_render_test.ls — render parsed/filtered packets to JSON, text, HTML.
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }
def has(&Str hay, Str needle, Str msg) { if !hay.contains?(needle) { @print(msg); @print(hay); c.abort() } }

def st9(int eaxc, int v0, int v1) -> CplaneSt9 {
    CplaneSt9 m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2, payload_size: 14, rtcid_pcid: eaxc, seqid: 1 }
    m.hdr = CpCommonHdr{ dir: false, payload_ver: 1, filter_idx: 0, frame_id: 7,
        subframe_id: 0, slot_id: 0, start_symbol_id: 3, num_sections: 1, section_type: 9 }
    m.num_sinr_per_prb = 1  m.oru_ctrl_slot_mask_id = 1  m.iq_width = 8  m.comp_meth = 0
    SecType9 s = {}
    s.section_id = 1  s.start_prbu = 0  s.num_prbu = 2
    SinrPrb p0 = {}  p0.sinr_raw.push(v0)
    SinrPrb p1 = {}  p1.sinr_raw.push(v1)
    s.prbs.push(p0)  s.prbs.push(p1)
    m.sections.push(s)
    return m
}

def st1ctrl() -> CplaneSt1 {
    CplaneSt1 m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2, payload_size: 20, rtcid_pcid: 0x0102, seqid: 1 }
    m.hdr = CpCommonHdr{ dir: true, payload_ver: 1, filter_idx: 0, frame_id: 7,
        subframe_id: 0, slot_id: 1, start_symbol_id: 0, num_sections: 1, section_type: 1 }
    m.ud_comp_hdr = 0
    m.sections.push(SecType1{ section_id: 9, rb: false, sym_inc: false,
        start_prbc: 0, num_prbc: 50, re_mask: 0xFFF, num_symbol: 14, ef: false, beam_id: 4 })
    return m
}

def main() {
    Writer w = oran.writer()
    CplaneSt1 ctrl = st1ctrl()
    CplaneSt9 ue = st9(0x0103, 18, 22)
    ctrl.write(&!w)
    ue.write(&!w)
    Reader r = w.as_reader()
    Vec(Packet) pkts = oran.parse_all(&!r, 8, 0)
    chk(pkts.len() == 2, "2 packets")

    // ---- JSON ----
    Str js = oran.to_json_all(pkts)
    has(js, "ST1", "json has ST1")
    has(js, "ST9_SINR", "json has ST9")
    has(js, "beamId", "json has beamId")
    has(js, "sinr", "json has sinr array")
    has(js, "258", "json has eaxc 0x0102=258")

    // ---- text ----
    Str tx = oran.to_text_all(pkts)
    has(tx, "ST1", "text has ST1")
    has(tx, "ST9", "text has ST9")
    has(tx, "mean=20", "text has mean SINR 20")

    // ---- HTML ----
    Str html = oran.to_html_all(pkts)
    has(html, "<table>", "html table open")
    has(html, "</table>", "html table close")
    has(html, "<td>ST9</td>", "html ST9 row")

    // ---- JSON of a filtered subset (only ST9) ----
    Vec(Packet) only9 = pkts.filter(|p| oran.section_type_of(p) == 9)
    Str js9 = oran.to_json_all(only9)
    has(js9, "ST9_SINR", "filtered json has ST9")
    chk(!js9.contains?("ST1"), "filtered json excludes ST1")

    @print("ORAN P5 RENDER PASS")
}
