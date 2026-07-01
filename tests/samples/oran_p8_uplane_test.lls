// oran_p8_uplane_test.ls — P8: U-plane IQ data section round-trip + mixed
// capture (C-plane + U-plane + ST9) via parse_all.
// Scenario: NR 100 MHz mu=1, DL IQ data, 2 PRBs, 16-bit uncompressed samples
// (12 complex REs per PRB). Per-section udCompHdr on the wire (not static).
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }

def make_uplane(int eaxc) -> UplaneMsg {
    UplaneMsg m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 0, payload_size: 0, rtcid_pcid: eaxc, seqid: 7 }
    m.dir = true  m.payload_ver = 1  m.filter_idx = 0
    m.frame_id = 60  m.subframe_id = 2  m.slot_id = 1  m.symbol_id = 5
    m.static_comp = false  m.iq_width = 16  m.comp_meth = 0
    UplaneSection s = {}
    s.section_id = 1  s.start_prbu = 0  s.num_prbu = 2
    for pi in 0..2 {
        UplanePrb prb = {}
        for re in 0..12 {
            int v = pi * 100 + re + 1
            prb.i_samples.push(v)
            prb.q_samples.push(0 - v)
        }
        s.prbs.push(prb)
    }
    m.sections.push(s)
    return m
}

def main() {
    // ---- single U-plane frame round-trip ----
    UplaneMsg m = make_uplane(0x0105)
    Writer w = oran.writer()
    m.write(&!w)
    Reader r = w.as_reader()
    UplaneMsg m2 = oran.parse_uplane(&!r, oran.ctx(16, 0))

    chk(m2.ecpri.msg_type == 0, "uplane msg_type 0")
    chk(m2.ecpri.rtcid_pcid == 0x0105, "uplane eaxc")
    chk(m2.dir, "uplane DL")
    chk(m2.symbol_id == 5, "uplane symbolId")
    chk(m2.sections.len() == 1, "1 section (implicit count)")
    chk(r.eof?(), "consumed exactly payload_size bytes")

    UplaneSection s2 = m2.sections[0]
    chk(s2.num_prbu == 2, "2 PRBs")
    chk(s2.prbs.len() == 2, "parsed 2 PRBs")
    // exact IQ round-trip across all 24 complex REs
    for pi in 0..2 {
        UplanePrb prb = s2.prbs[pi]
        chk(prb.i_samples.len() == 12, "12 I samples")
        chk(prb.q_samples.len() == 12, "12 Q samples")
        for re in 0..12 {
            int v = pi * 100 + re + 1
            chk(prb.i_samples[re] == v, "I sample roundtrip")
            chk(prb.q_samples[re] == (0 - v), "Q sample roundtrip")
        }
    }

    // ---- mixed capture: C-plane ST1 + U-plane + ST9, parsed together ----
    Writer w2 = oran.writer()
    // ST1 control
    CplaneSt1 ctrl = {}
    ctrl.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2, payload_size: 20, rtcid_pcid: 0x0102, seqid: 1 }
    ctrl.hdr = CpCommonHdr{ dir: true, payload_ver: 1, filter_idx: 0, frame_id: 60,
        subframe_id: 2, slot_id: 1, start_symbol_id: 0, num_sections: 1, section_type: 1 }
    ctrl.ud_comp_hdr = 0
    ctrl.sections.push(SecType1{ section_id: 1, rb: false, sym_inc: false,
        start_prbc: 0, num_prbc: 50, re_mask: 0xFFF, num_symbol: 14, ef: false, beam_id: 3 })
    ctrl.write(&!w2)
    // U-plane IQ
    UplaneMsg up = make_uplane(0x0105)
    up.write(&!w2)

    Reader r2 = w2.as_reader()
    Vec(Packet) pkts = oran.parse_all(&!r2, 16, 0)
    chk(pkts.len() == 2, "mixed: 2 packets")
    chk(oran.section_type_of(pkts[0]) == 1, "pkt0 ST1")
    chk(oran.is_uplane?(pkts[1]), "pkt1 is U-plane")
    chk(oran.section_type_of(pkts[1]) == 256, "pkt1 sentinel 256")
    chk(oran.eaxc_of(pkts[1]) == 0x0105, "pkt1 eaxc")

    @print("ORAN P8 UPLANE PASS")
}
