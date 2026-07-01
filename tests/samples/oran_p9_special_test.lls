// oran_p9_special_test.ls — special field values.
//  * numPrbu = 0 means "all PRBs in the carrier" (NR >255-PRB case, e.g. 273 PRBs
//    for 100 MHz mu=1). The receiver must use the cell's carrier PRB count
//    (ParseCtx.carrier_prbs) — otherwise it parses 0 PRBs and misaligns.
//  * udIqWidth = 0 on the wire means 16-bit samples.
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }

def main() {
    // U-plane message that signals numPrbu = 0 (all PRBs) and carries 4 PRBs of IQ.
    UplaneMsg m = {}
    m.ecpri = EcpriHdr{ version: 1, concat: false, msg_type: 0, payload_size: 0, rtcid_pcid: 0x0102, seqid: 1 }
    m.dir = true  m.payload_ver = 1  m.filter_idx = 0
    m.frame_id = 1  m.subframe_id = 0  m.slot_id = 0  m.symbol_id = 0
    m.static_comp = false  m.iq_width = 16  m.comp_meth = 0
    UplaneSection s = {}
    s.section_id = 1  s.start_prbu = 0  s.num_prbu = 0       // 0 = all PRBs
    for pp in 0..4 {
        UplanePrb prb = {}
        for re in 0..12 { prb.i_samples.push(pp * 100 + re)  prb.q_samples.push(0 - (pp * 100 + re)) }
        s.prbs.push(prb)
    }
    m.sections.push(s)

    Writer w = oran.writer()
    m.write(&!w)

    // a named default profile carries the cell's carrier PRB count
    chk(oran.profile_nr_100mhz_scs30().carrier_prbs == 273, "NR 100MHz mu=1 profile = 273 PRBs")
    chk(oran.profile_nr_50mhz_scs30().carrier_prbs == 133, "NR 50MHz mu=1 profile = 133 PRBs")

    // parse WITH a profile giving the carrier PRB count (=4 here) -> all 4 PRBs recovered
    Reader r = w.as_reader()
    UplaneMsg g = oran.parse_uplane(&!r, oran.profile(100, 30, 4))
    UplaneSection gs = g.sections[0]
    chk(gs.num_prbu == 0, "numPrbu field is 0 on the wire (all-PRBs flag)")
    chk(gs.prbs.len() == 4, "carrier_prbs=4 -> 4 PRBs parsed")
    chk(gs.prbs[0].i_samples[0] == 0, "prb0 sample0")
    chk(gs.prbs[3].i_samples[11] == 311, "prb3 sample11 (3*100+11)")
    chk(gs.prbs[3].q_samples[11] == (0 - 311), "prb3 Q sample11")
    chk(r.eof?(), "consumed exactly the frame (no misalignment)")

    // udIqWidth = 0 on the wire decodes to 16-bit
    chk(gs.iq_width == 16, "udIqWidth=0 means 16-bit samples")

    @print("ORAN SPECIAL PASS")
}
