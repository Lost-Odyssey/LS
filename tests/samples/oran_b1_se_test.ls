// oran_b1_se_test.ls — B1: Section Extension field decoders (Pass 2).
// Build SE6/SE10/SE18/SE5 as a chain on an ST1 section + SE12 on another,
// round-trip through write_se_chain/parse_se_chain, decode and assert fields.
import std.sys.c as c
import std.text.bytes as b
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }

def main() {
    // ---- ST1 section carrying SE6 + SE10 + SE18 + SE5 ----
    SecType1 s = {}
    s.section_id = 1  s.start_prbc = 0  s.num_prbc = 50  s.re_mask = 0xFFF
    s.num_symbol = 14  s.beam_id = 9  s.ef = true
    s.exts.push(oran.make_se6(1, 2, 0x1234567, 2, 0x2481))   // rep,rbgSize,rbgMask,prio,symMask
    Vec(int) ids = [10, 20, 30]
    s.exts.push(oran.make_se10(2, 3, &ids))                  // vector listing, 3 ports
    s.exts.push(oran.make_se18(0x1234, 0x1ABC, 1))           // txWinOff,txWinSize,toT
    Vec(McSet) sets = {}
    McSet m0 = {}  m0.mc_scale_re_mask = 0xABC  m0.csf = true   m0.mc_scale_offset = 0x1234
    McSet m1 = {}  m1.mc_scale_re_mask = 0x555  m1.csf = false  m1.mc_scale_offset = 0x0FF
    sets.push(m0)  sets.push(m1)
    s.exts.push(oran.make_se5(&sets))

    Writer w = oran.writer()
    s.write(&!w)
    Reader r = w.as_reader()
    SecType1 s2 = oran.parse_st1_section(&!r)
    chk(s2.exts.len() == 4, "4 SEs in chain")
    chk(r.eof?(), "consumed whole section + SE chain")

    Se6 e6 = oran.find_se6(&s2.exts)
    chk(e6.present, "SE6 present")
    chk(e6.repetition == 1, "se6 repetition")
    chk(e6.rbg_size == 2, "se6 rbgSize")
    chk(e6.rbg_mask == 0x1234567, "se6 rbgMask")
    chk(e6.priority == 2, "se6 priority")
    chk(e6.symbol_mask == 0x2481, "se6 symbolMask")

    Se10 e10 = oran.find_se10(&s2.exts)
    chk(e10.present, "SE10 present")
    chk(e10.beam_group_type == 2, "se10 type")
    chk(e10.num_portc == 3, "se10 numPortc")
    chk(e10.ids.len() == 3, "se10 3 ids")
    chk(e10.ids[0] == 10, "se10 id0")
    chk(e10.ids[2] == 30, "se10 id2")

    Se18 e18 = oran.find_se18(&s2.exts)
    chk(e18.present, "SE18 present")
    chk(e18.tx_window_offset == 0x1234, "se18 offset")
    chk(e18.tx_window_size == 0x1ABC, "se18 size")
    chk(e18.tot == 1, "se18 toT")

    Se5 e5 = oran.find_se5(&s2.exts)
    chk(e5.present, "SE5 present")
    chk(e5.sets.len() == 2, "se5 2 sets")
    chk(e5.sets[0].mc_scale_re_mask == 0xABC, "se5 set0 reMask")
    chk(e5.sets[0].csf, "se5 set0 csf")
    chk(e5.sets[0].mc_scale_offset == 0x1234, "se5 set0 offset")
    chk(e5.sets[1].mc_scale_re_mask == 0x555, "se5 set1 reMask")
    chk(!e5.sets[1].csf, "se5 set1 csf")
    chk(e5.sets[1].mc_scale_offset == 0x0FF, "se5 set1 offset")

    // ---- separate ST1 section carrying SE12 ----
    SecType1 t = {}
    t.section_id = 2  t.start_prbc = 0  t.num_prbc = 100  t.re_mask = 0xFFF
    t.num_symbol = 14  t.beam_id = 1  t.ef = true
    Vec(int) off = [5, 10]
    Vec(int) num = [20, 30]
    t.exts.push(oran.make_se12(1, 0x0F0F, &off, &num))
    Writer w2 = oran.writer()
    t.write(&!w2)
    Reader r2 = w2.as_reader()
    SecType1 t2 = oran.parse_st1_section(&!r2)
    Se12 e12 = oran.find_se12(&t2.exts)
    chk(e12.present, "SE12 present")
    chk(e12.priority == 1, "se12 priority")
    chk(e12.symbol_mask == 0x0F0F, "se12 symbolMask")
    chk(e12.off_start_prb.len() == 2, "se12 2 ranges")
    chk(e12.off_start_prb[0] == 5, "se12 off0")
    chk(e12.off_start_prb[1] == 10, "se12 off1")
    chk(e12.num_prb[0] == 20, "se12 num0")
    chk(e12.num_prb[1] == 30, "se12 num1")

    @print("ORAN B1 SE PASS")
}
