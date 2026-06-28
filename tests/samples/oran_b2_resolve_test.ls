// oran_b2_resolve_test.ls — B2: Pass-3 semantic resolution. The headline check:
// SE6's symbolMask/rbgMask OVERRIDE the section's base startSymbolId/numSymbol/
// startPrbc, so resolve_st1_section returns the real symbols/PRBs, not the base.
import std.sys.c as c
import oran.cus as oran

def chk(bool ok, Str msg) { if !ok { @print(msg); c.abort() } }

def main() {
    // ---- section WITH SE6: base says startSymbolId=3, numSymbol=1, but SE6
    //      symbolMask selects symbols {7,10,13}; rbgMask selects RBG {0,2} of
    //      size-4 groups over PRBs 0..11 -> PRBs {0,1,2,3, 8,9,10,11}.
    SecType1 s = {}
    s.section_id = 1  s.start_prbc = 0  s.num_prbc = 12  s.re_mask = 0xFFF
    s.num_symbol = 1  s.beam_id = 5  s.rb = false  s.ef = true
    // symbolMask bits 7,10,13 = (1<<7)|(1<<10)|(1<<13) = 0x2480 ; rbgMask 0b101 = 0x5
    s.exts.push(oran.make_se6(0, 4, 0x5, 0, 0x2480))
    Vec(int) ids = [20, 21]
    s.exts.push(oran.make_se10(2, 2, &ids))     // vector listing: ports get beam 20,21

    EffectiveAlloc ea = oran.resolve_st1_section(&s, 3, 273)   // start_symbol_id=3

    // symbols come from symbolMask, NOT the base startSymbolId(3)/numSymbol(1)
    chk(ea.symbols.len() == 3, "3 symbols from symbolMask")
    chk(ea.symbols[0] == 7, "symbol 7")
    chk(ea.symbols[1] == 10, "symbol 10")
    chk(ea.symbols[2] == 13, "symbol 13")
    chk(ea.symbols[0] != 3, "SE6 overrode startSymbolId (not 3)")

    // PRBs from rbgMask: RBG0 -> [0..3], RBG2 -> [8..11]
    chk(ea.prbs.len() == 8, "8 PRBs from rbgMask")
    chk(ea.prbs[0] == 0, "prb 0")
    chk(ea.prbs[3] == 3, "prb 3 (end of RBG0)")
    chk(ea.prbs[4] == 8, "prb 8 (start of RBG2)")
    chk(ea.prbs[7] == 11, "prb 11")

    // beams: base 5 + SE10 listed [20,21]
    chk(ea.beams.len() == 3, "3 beams (base + 2 ports)")
    chk(ea.beams[0] == 5, "base beam 5")
    chk(ea.beams[1] == 20, "se10 beam 20")
    chk(ea.beams[2] == 21, "se10 beam 21")

    // ---- contrast: section WITHOUT SE6 -> base fields used directly ----
    SecType1 b = {}
    b.section_id = 2  b.start_prbc = 5  b.num_prbc = 3  b.re_mask = 0xFFF
    b.num_symbol = 2  b.beam_id = 7  b.rb = false  b.ef = false
    EffectiveAlloc eb = oran.resolve_st1_section(&b, 4, 273)   // start_symbol_id=4
    chk(eb.symbols.len() == 2, "base: 2 symbols")
    chk(eb.symbols[0] == 4, "base symbol = startSymbolId 4")
    chk(eb.symbols[1] == 5, "base symbol +1")
    chk(eb.prbs.len() == 3, "base: 3 PRBs")
    chk(eb.prbs[0] == 5, "base prb = startPrbc 5")
    chk(eb.prbs[2] == 7, "base prb +2")
    chk(eb.beams.len() == 1, "base: 1 beam")
    chk(eb.beams[0] == 7, "base beam 7")

    // ---- SE12 ranges: startPrbc=10, ranges (off=2,num=3)+(off=5,num=2) ----
    SecType1 t = {}
    t.section_id = 3  t.start_prbc = 10  t.num_prbc = 50  t.num_symbol = 1  t.beam_id = 1  t.ef = true
    Vec(int) off = [2, 5]
    Vec(int) num = [3, 2]
    t.exts.push(oran.make_se12(0, 0x0001, &off, &num))   // symbolMask bit0 only
    EffectiveAlloc et = oran.resolve_st1_section(&t, 0, 273)
    chk(et.symbols.len() == 1, "se12 symbol")
    chk(et.symbols[0] == 0, "se12 symbol 0")
    // range0: 10+2=12 -> [12,13,14]; range1: 15+5=20 -> [20,21]
    chk(et.prbs.len() == 5, "se12 5 PRBs")
    chk(et.prbs[0] == 12, "se12 prb 12")
    chk(et.prbs[2] == 14, "se12 prb 14")
    chk(et.prbs[3] == 20, "se12 prb 20")
    chk(et.prbs[4] == 21, "se12 prb 21")

    @print("ORAN B2 RESOLVE PASS")
}
