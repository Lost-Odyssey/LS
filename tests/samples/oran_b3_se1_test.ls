// oran_b3_se1_test.ls — B3: SE1 beamforming weights. Decode K=num_trx complex
// weights (bit-packed signed IQ), uncompressed and BFP, round-trip.
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }

def main() {
    int K = 4     // 4 TRX / antennas

    // ---- SE1 uncompressed (bfwCompMeth=0), 16-bit weights ----
    Vec(int) wi = [100, -200, 300, -400]
    Vec(int) wq = [-50, 60, -70, 80]
    SecType1 s = {}
    s.section_id = 1  s.start_prbc = 0  s.num_prbc = 50  s.re_mask = 0xFFF
    s.num_symbol = 14  s.beam_id = 3  s.ef = true
    s.exts.push(oran.make_se1(16, 0, 0, &wi, &wq))
    Writer w = oran.writer()
    s.write(&!w)
    Reader r = w.as_reader()
    SecType1 s2 = oran.parse_st1_section(&!r)
    Se1 e1 = oran.find_se1(&s2.exts, K)
    chk(e1.present, "SE1 present")
    chk(e1.bfw_iq_width == 16, "bfwIqWidth = 16")
    chk(e1.bfw_comp_meth == 0, "no compression")
    chk(e1.bfw_i.len() == 4, "4 I weights")
    chk(e1.bfw_q.len() == 4, "4 Q weights")
    chk(e1.bfw_i[0] == 100, "wi0")
    chk(e1.bfw_i[1] == -200, "wi1 (signed)")
    chk(e1.bfw_i[3] == -400, "wi3")
    chk(e1.bfw_q[0] == -50, "wq0")
    chk(e1.bfw_q[3] == 80, "wq3")

    // ---- SE1 BFP (bfwCompMeth=1), 9-bit mantissas + exponent 3 ----
    Vec(int) bi = [10, -20, 30, -40]
    Vec(int) bq = [5, -6, 7, -8]
    SecType1 t = {}
    t.section_id = 2  t.start_prbc = 0  t.num_prbc = 50  t.re_mask = 0xFFF
    t.num_symbol = 14  t.beam_id = 1  t.ef = true
    t.exts.push(oran.make_se1(9, 1, 3, &bi, &bq))
    Writer w2 = oran.writer()
    t.write(&!w2)
    Reader r2 = w2.as_reader()
    SecType1 t2 = oran.parse_st1_section(&!r2)
    Se1 e1b = oran.find_se1(&t2.exts, K)
    chk(e1b.bfw_iq_width == 9, "bfp width 9")
    chk(e1b.bfw_comp_meth == 1, "BFP")
    chk(e1b.bfw_comp_param == 3, "exponent 3")
    chk(e1b.bfw_i[0] == 10, "bfp wi0 mantissa")
    chk(e1b.bfw_i[1] == -20, "bfp wi1 (signed)")
    chk(e1b.bfw_q[3] == -8, "bfp wq3")
    // decoded weight value = mantissa * 2^exponent (reuse iq_decode)
    chk((oran.iq_decode(e1b.bfw_i[0], e1b.bfw_comp_param, 1) as int) == 80, "decoded wi0 = 10*2^3 = 80")
    chk((oran.iq_decode(e1b.bfw_i[1], e1b.bfw_comp_param, 1) as int) == (0 - 160), "decoded wi1 = -20*8")

    @print("ORAN B3 SE1 PASS")
}
