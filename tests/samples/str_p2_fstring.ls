// P2 self-verifying (docs/plan_string_to_stdlib.md §5.2): an f-string with
// interpolations produces an OWNED Str rvalue (cap>0, heap), routed through
// the unified has_drop temp/drop path — no leak, no double-free. Since P5-2
// the f-string default IS Str. JIT+AOT+memcheck.
import std.core.str
import std.core.vec

struct Box { Str s; int n }

def check(bool ok, Str what) {
    if !ok { @print(f"STRP2 FAIL: {what}") }
}

def take(Str s) -> int { return s.len() }
def make(int k) -> Str { return f"v={k}" }

def main() {
    int n = 42
    Str who = "ann"

    // var-decl: owned Str straight from an f-string
    Str a = f"hi {who} #{n}"
    check(a.eq?("hi ann #42"), "fstr decl")
    check(a.len() == 10, "fstr len")
    check(a.cap() > 0, "fstr owned")          // heap-owned (cap>0)

    // clone of an OWNED f-string Str must be an independent deep copy
    Str b = a.__clone()
    b.push_byte(63)                            // '?'
    check(b.eq?("hi ann #42?"), "fstr clone mutate")
    check(a.eq?("hi ann #42"), "fstr clone isolation")

    // move of an owned f-string Str
    Str c = f"move-{n}"
    Str d = c
    check(d.eq?("move-42"), "fstr move")

    // f-string into a param slot (clone of owned = independent owned)
    check(take(f"abc{n}") == 5, "fstr param")  // "abc42" -> 5 bytes

    // f-string into a return slot
    Str r = make(7)
    check(r.eq?("v=7"), "fstr return")

    // f-string into a struct field
    Box bx = Box { s: f"f{n}", n: 1 }
    check(bx.s.eq?("f42"), "fstr field")

    // f-string pushed into Vec(Str) (owned element, drop all)
    Vec(Str) v = {}
    v.push(f"e{n}")
    v.push(f"q")
    check(v.len() == 2, "fstr vec len")
    check(v.get!(0).eq?("e42"), "fstr vec elem")

    // mixing static-literal Str and owned f-string Str in the same scope
    Str lit = "static"
    check(lit.eq?("static"), "mix static")

    @print("STRP2 PASS")
}
