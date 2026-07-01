// Stage 1 enum derive: @derive(Equal, Hash, Order, Show) over variants (incl.
// POD + Str payloads). Equal+Hash make the enum a usable Map key; Order gives a
// lexicographic ordering (variant declaration order, then payloads); Show renders
// a variant name + parenthesized payloads via each payload's .show().
import std.core.map

@derive(Equal, Hash, Order, Show)
enum Tok { Num(int); Word(Str); End }

def main() {
    Tok a = Num(7)
    Tok b = Num(7)
    Tok c = Word("hi")
    if a == b { @print("e_eq1 PASS") } else { @print("e_eq1 FAIL") }
    if a == c { @print("e_eq2 FAIL") } else { @print("e_eq2 PASS") }

    // Directly comparing two rvalue literals of a has_drop enum (POD + Str
    // payload). This used to mis-evaluate: the operator-call receiver temp was
    // dropped before the comparison ran. Both operands now stay live.
    if Num(7) == Num(7) { @print("e_rv1 PASS") } else { @print("e_rv1 FAIL") }
    if Num(7) == Num(9) { @print("e_rv2 FAIL") } else { @print("e_rv2 PASS") }
    if Word("hi") == Word("hi") { @print("e_rv3 PASS") } else { @print("e_rv3 FAIL") }
    if Word("hi") == Word("no") { @print("e_rv4 FAIL") } else { @print("e_rv4 PASS") }

    Map(Tok, int) m = {}
    m.set(Num(7), 100)
    m.set(Word("hi"), 200)
    m.set(End, 300)
    @print(m.get(Num(7)).unwrap_or(0))      // 100
    @print(m.get(Word("hi")).unwrap_or(0))  // 200
    @print(m.get(End).unwrap_or(0))         // 300
    @print(m.get(Num(9)).unwrap_or(0 - 1))  // -1 (absent)

    // ---- Show: variant name + parenthesized payloads (each via .show()) ----
    @print(to_str(a))                        // Num(7)
    @print(to_str(c))                        // Word(hi)
    Tok en = End
    @print(to_str(en))                       // End

    // ---- Order: declaration order Num < Word < End, then payload lexicographic ----
    if Num(7) < Word("hi") { @print("e_ord1 PASS") } else { @print("e_ord1 FAIL") }
    if Word("hi") < End { @print("e_ord2 PASS") } else { @print("e_ord2 FAIL") }
    if Num(7) < Num(9) { @print("e_ord3 PASS") } else { @print("e_ord3 FAIL") }
    if Num(9) < Num(7) { @print("e_ord4 FAIL") } else { @print("e_ord4 PASS") }
    if End < Num(7) { @print("e_ord5 FAIL") } else { @print("e_ord5 PASS") }

    @print("DERIVE ENUM DONE")
}
