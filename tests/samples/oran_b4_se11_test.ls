// oran_b4_se11_test.ls — B4: SE11 flexible beamforming weights per PRB bundle.
// numPrbc=8, numBundPrb=4 -> 2 bundles; each has contInd+beamId + L=2 complex
// weights (bit-packed). Round-trip build -> parse -> decode.
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }

def main() {
    int L = 2     // 2 TRX

    Vec(BundleW) bundles = {}
    BundleW b0 = {}  b0.cont_ind = false  b0.beam_id = 100
    b0.bfw_i.push(10)  b0.bfw_i.push(20)  b0.bfw_q.push(0 - 10)  b0.bfw_q.push(0 - 20)
    BundleW b1 = {}  b1.cont_ind = true   b1.beam_id = 200
    b1.bfw_i.push(30)  b1.bfw_i.push(40)  b1.bfw_q.push(0 - 30)  b1.bfw_q.push(0 - 40)
    bundles.push(b0)  bundles.push(b1)

    SecType1 s = {}
    s.section_id = 1  s.start_prbc = 0  s.num_prbc = 8  s.re_mask = 0xFFF
    s.num_symbol = 14  s.beam_id = 1  s.ef = true
    // disableBFWs=0, RAD=0, bundleOffset=0, numBundPrb=4, bfwIqWidth=16, compMeth=0
    s.exts.push(oran.make_se11(false, 0, 0, 4, 16, 0, &bundles))

    Writer w = oran.writer()
    s.write(&!w)
    Reader r = w.as_reader()
    SecType1 s2 = oran.parse_st1_section(&!r)
    Se11 e11 = oran.find_se11(&s2.exts, 8, L)     // numPrbc=8, L=2 TRX

    chk(e11.present, "SE11 present")
    chk(!e11.disable_bfws, "disableBFWs = 0")
    chk(e11.num_bund_prb == 4, "numBundPrb = 4")
    chk(e11.bfw_iq_width == 16, "bfwIqWidth = 16")
    chk(e11.bfw_comp_meth == 0, "no compression")
    chk(e11.bundles.len() == 2, "2 bundles = ceil(8/4)")

    BundleW d0 = e11.bundles[0]
    chk(!d0.cont_ind, "bundle0 contInd 0")
    chk(d0.beam_id == 100, "bundle0 beamId 100")
    chk(d0.bfw_i.len() == 2, "bundle0 L=2 weights")
    chk(d0.bfw_i[0] == 10, "b0 wi0")
    chk(d0.bfw_i[1] == 20, "b0 wi1")
    chk(d0.bfw_q[0] == (0 - 10), "b0 wq0")
    chk(d0.bfw_q[1] == (0 - 20), "b0 wq1")

    BundleW d1 = e11.bundles[1]
    chk(d1.cont_ind, "bundle1 contInd 1")
    chk(d1.beam_id == 200, "bundle1 beamId 200")
    chk(d1.bfw_i[0] == 30, "b1 wi0")
    chk(d1.bfw_q[1] == (0 - 40), "b1 wq1")

    @print("ORAN B4 SE11 PASS")
}
