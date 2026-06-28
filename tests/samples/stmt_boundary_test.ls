// stmt_boundary_test.ls — parser statement-boundary regressions.
//
// L-003: a line-leading `*K p` / `*V p` pointer var decl (generic type param)
//        right after a value-ending statement must NOT be swallowed as infix
//        multiplication (`self.x = 8` ⏎ `*K p` was parsed `8 * K`).
// L-004: an if/while condition that STARTS with `(` and continues with an infix
//        op (`while (e) != x {`) must keep the trailing op (was `expected '{'`).
// Plus the false-positive guards: genuine multiplication (same-line and the rare
// cross-line `a` ⏎ `* b`) must stay multiplication.
import std.sys.c as c
import std.core.str
import std.core.map

struct Pair(K, V) { object kbuf; object vbuf; int sz }

methods(K, V) Pair(K, V) {
    def build(&!self) {
        self.sz = 8                      // value-ending stmt, then a `*K` decl
        *K kp = c.malloc(8) as *K
        self.kbuf = nil                  // value-ending stmt (nil), then `*V` decl
        *V vp = c.malloc(8) as *V
        self.kbuf = kp as object
        self.vbuf = vp as object
    }
}

def check(bool cond, Str label) {
    if (cond) { @print(f"PASS {label}") } else { @print(f"STMT FAIL {label}") }
}

def main() -> int {
    // ---- L-003 ----
    Pair(int, int) pp = {}
    pp.build()
    check(pp.sz == 8, "l003-generic-ptr-decl")

    // ---- L-004 ----
    int x = 0
    while (x + 0) < 255 { x = x + 50 }        // cond starts with '(' + infix
    check(x == 300, "l004-while-paren-infix")
    int got = 0
    if (x + 0) != 255 { got = 1 } else { got = 2 }
    check(got == 1, "l004-if-paren-infix")
    int y = 7
    if (y as int) > 5 { check(true, "l004-if-cast-paren") }
    if ((y * 2) - 5) == 9 { check(true, "l004-nested-group") }
    if (y > 0) { check(true, "l004-plain-paren") }   // plain paren still ok

    // ---- multiplication false-positive guards ----
    int a = 6
    int b = 7
    int r1 = a * b                            // same-line ident*ident
    check(r1 == 42, "mult-same-line")
    int r2 = a
        * b                                   // cross-line mult (single operand)
    check(r2 == 42, "mult-cross-line")

    // ---- L-005: a same-line `recv.method(literal args) ident = …` must parse
    //      as two statements (method call + assign), NOT a qualified generic type
    //      decl `recv.method(1,2) ident`. Literal args can never be type args. ----
    Pair(int, int) qq = {}
    int c = 0
    qq.build() c = c + 1                       // method call then assign on one line
    check(c == 1, "l005-sameline-call-then-assign")
    Map(int, int) m = {}
    m.set(1, 2) c = c + 5                       // qualified method(literals) + assign
    check(c == 6, "l005-qualified-method-literals")
    // valid qualified generic decl with TYPE args still parses as a decl
    Map(Str, int) m2 = {}
    check(m2.len() == 0, "l005-generic-typeargs-still-decl")

    @print("STMT OK")
    return 0
}
