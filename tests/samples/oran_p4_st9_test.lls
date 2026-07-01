// oran_p4_st9_test.ls — P4: Section Type 9 SINR report round-trip + mean SINR.
// Scenario: NR 100 MHz mu=1, O-RU reports post-equalization SINR, 2 SINR values
// per PRB, 3 PRBs, 8-bit uncompressed samples. SINR encoded directly in dB.
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }
def chkf(f64 got, f64 want, Str msg) {
    f64 d = got - want
    if d < 0.0 { d = 0.0 - d }
    if d > 0.001 { @print(msg); @print(got); c.abort() }
}

def main() {
    CplaneSt9 m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 2,
                        payload_size: 22, rtcid_pcid: 0x0102, seqid: 0x0040 }
    m.hdr = CpCommonHdr{ dir: false, payload_ver: 1, filter_idx: 0,
        frame_id: 200, subframe_id: 5, slot_id: 0, start_symbol_id: 3,  // UL, symbolId=3
        num_sections: 1, section_type: 9 }
    m.num_sinr_per_prb = 2
    m.oru_ctrl_slot_mask_id = 1
    m.iq_width = 8
    m.comp_meth = 0          // uncompressed

    SecType9 s = {}
    s.section_id = 7
    s.rb = false
    s.sym_inc = false
    s.start_prbu = 0
    s.num_prbu = 3
    // 3 PRBs x 2 sub-band SINR values (dB): mean = 90/6 = 15.0
    SinrPrb p0 = {}  p0.sinr_raw.push(10)  p0.sinr_raw.push(20)
    SinrPrb p1 = {}  p1.sinr_raw.push(14)  p1.sinr_raw.push(16)
    SinrPrb p2 = {}  p2.sinr_raw.push(12)  p2.sinr_raw.push(18)
    s.prbs.push(p0)  s.prbs.push(p1)  s.prbs.push(p2)
    m.sections.push(s)

    Writer w = oran.writer()
    m.write(&!w)
    // 8 (ecpri) + 6 (common) + 2 (o15/16) + 4 (sec hdr) + 3*2 (PRB samples) = 26
    chk(w.size() == 26, "st9 total size should be 26")

    Reader r = w.as_reader()
    CplaneSt9 m2 = oran.parse_st9(&!r, 8, 0, 0)

    chk(m2.hdr.section_type == 9, "stype 9")
    chk(!m2.hdr.dir, "UL")
    chk(m2.hdr.start_symbol_id == 3, "symbolId 3")
    chk(m2.num_sinr_per_prb == 2, "nsinr 2")
    chk(m2.sections.len() == 1, "1 section")

    SecType9 s2 = m2.sections[0]
    chk(s2.section_id == 7, "sec id 7")
    chk(s2.num_prbu == 3, "3 prbs")
    chk(s2.prbs.len() == 3, "parsed 3 prbs")

    // exact round-trip of all SINR values
    chk(s2.prbs[0].sinr_raw[0] == 10, "p0v0")
    chk(s2.prbs[0].sinr_raw[1] == 20, "p0v1")
    chk(s2.prbs[1].sinr_raw[0] == 14, "p1v0")
    chk(s2.prbs[2].sinr_raw[1] == 18, "p2v1")

    // mean SINR over the section (inline; Packet-level stats come in the next phase)
    f64 sum = 0.0
    int cnt = 0
    for pi in 0..s2.prbs.len() {
        SinrPrb prb = s2.prbs[pi]
        for ki in 0..prb.sinr_raw.len() {
            sum = sum + oran.sinr_decode(prb.sinr_raw[ki], prb.comp_param, 0)
            cnt = cnt + 1
        }
    }
    chk(cnt == 6, "6 sinr values")
    chkf(sum / (cnt as f64), 15.0, "mean SINR should be 15.0")

    @print("ORAN P4 ST9 PASS")
}
