// oran_p4_multi_test.ls — P4: multi-frame parse_all over back-to-back eCPRI
// frames (one ST1 DL control + one ST9 SINR report), with field accessors.
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }

def make_st1() -> CplaneSt1 {
    CplaneSt1 m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2, payload_size: 20, rtcid_pcid: 0x0102, seqid: 1 }
    m.hdr = CpCommonHdr{ dir: true, payload_ver: 1, filter_idx: 0, frame_id: 50,
        subframe_id: 0, slot_id: 0, start_symbol_id: 0, num_sections: 1, section_type: 1 }
    m.ud_comp_hdr = 0
    m.sections.push(SecType1{ section_id: 1, rb: false, sym_inc: false,
        start_prbc: 0, num_prbc: 273 & 0xFF, re_mask: 0xFFF, num_symbol: 14, ef: false, beam_id: 5 })
    return m
}

def make_st9() -> CplaneSt9 {
    CplaneSt9 m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2, payload_size: 14, rtcid_pcid: 0x0103, seqid: 2 }
    m.hdr = CpCommonHdr{ dir: false, payload_ver: 1, filter_idx: 0, frame_id: 50,
        subframe_id: 0, slot_id: 0, start_symbol_id: 3, num_sections: 1, section_type: 9 }
    m.num_sinr_per_prb = 1
    m.oru_ctrl_slot_mask_id = 1
    m.iq_width = 8
    m.comp_meth = 0
    SecType9 s = {}
    s.section_id = 3  s.start_prbu = 0  s.num_prbu = 2
    SinrPrb p0 = {}  p0.sinr_raw.push(18)
    SinrPrb p1 = {}  p1.sinr_raw.push(22)
    s.prbs.push(p0)  s.prbs.push(p1)
    m.sections.push(s)
    return m
}

def main() {
    // write two frames back-to-back into one buffer
    Writer w = oran.writer()
    CplaneSt1 a = make_st1()
    CplaneSt9 d = make_st9()
    a.write(&!w)
    d.write(&!w)

    Reader r = w.as_reader()
    Vec(Packet) pkts = oran.parse_all(&!r, 8, 0)
    chk(pkts.len() == 2, "should parse 2 packets")

    // accessors via auto-borrow of indexed elements
    chk(oran.section_type_of(pkts[0]) == 1, "pkt0 is ST1")
    chk(oran.section_type_of(pkts[1]) == 9, "pkt1 is ST9")
    chk(oran.eaxc_of(pkts[0]) == 0x0102, "pkt0 eaxc")
    chk(oran.eaxc_of(pkts[1]) == 0x0103, "pkt1 eaxc")
    chk(oran.is_dl?(pkts[0]), "pkt0 DL")
    chk(!oran.is_dl?(pkts[1]), "pkt1 UL")

    @print("ORAN P4 MULTI PASS")
}
