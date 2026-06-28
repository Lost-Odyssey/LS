// oran_p4_stats_test.ls — P4 headline: filter packets by a closure rule, then
// compute mean SINR. Scenario: a capture with one ST1 control frame and two
// ST9 SINR reports from two different UEs (eAxC), mu=1 100 MHz.
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }
def chkf(f64 got, f64 want, Str msg) {
    f64 d = got - want
    if d < 0.0 { d = 0.0 - d }
    if d > 0.001 { @print(msg); @print(got); c.abort() }
}

def st9(int eaxc, int v0, int v1) -> CplaneSt9 {
    CplaneSt9 m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2, payload_size: 14, rtcid_pcid: eaxc, seqid: 1 }
    m.hdr = CpCommonHdr{ dir: false, payload_ver: 1, filter_idx: 0, frame_id: 50,
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
    m.hdr = CpCommonHdr{ dir: true, payload_ver: 1, filter_idx: 0, frame_id: 50,
        subframe_id: 0, slot_id: 0, start_symbol_id: 0, num_sections: 1, section_type: 1 }
    m.ud_comp_hdr = 0
    m.sections.push(SecType1{ section_id: 1, rb: false, sym_inc: false,
        start_prbc: 0, num_prbc: 100, re_mask: 0xFFF, num_symbol: 14, ef: false, beam_id: 1 })
    return m
}

def main() {
    // Build a capture: ST1 control + ST9(UE 0x0103: 18,22 dB) + ST9(UE 0x0104: 10,14 dB)
    Writer w = oran.writer()
    CplaneSt1 ctrl = st1ctrl()
    CplaneSt9 ue1 = st9(0x0103, 18, 22)
    CplaneSt9 ue2 = st9(0x0104, 10, 14)
    ctrl.write(&!w)
    ue1.write(&!w)
    ue2.write(&!w)

    Reader r = w.as_reader()
    Vec(Packet) pkts = oran.parse_all(&!r, 8, 0)
    chk(pkts.len() == 3, "3 packets")

    // mean SINR over ALL packets (ST1 contributes nothing): (18+22+10+14)/4 = 16
    chkf(oran.mean_sinr(pkts), 16.0, "overall mean SINR")

    // filter: only ST9 SINR reports
    Vec(Packet) sinrPkts = pkts.filter(|p| oran.section_type_of(p) == 9)
    chk(sinrPkts.len() == 2, "2 ST9 packets")

    // filter by UE (eAxC) then mean SINR
    Vec(Packet) ue1pk = pkts.filter(|p| oran.eaxc_of(p) == 0x0103)
    chk(ue1pk.len() == 1, "UE1 one packet")
    chkf(oran.mean_sinr(ue1pk), 20.0, "UE1 mean SINR = 20")

    Vec(Packet) ue2pk = pkts.filter(|p| oran.eaxc_of(p) == 0x0104)
    chkf(oran.mean_sinr(ue2pk), 12.0, "UE2 mean SINR = 12")

    // min / max over all SINR
    Vec(f64) all = oran.collect_sinr(pkts)
    chk(all.len() == 4, "4 sinr values")
    chkf(oran.minf(all), 10.0, "min SINR 10")
    chkf(oran.maxf(all), 22.0, "max SINR 22")

    @print("ORAN P4 STATS PASS")
}
