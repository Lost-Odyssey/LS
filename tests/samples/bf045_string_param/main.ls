/* BF-045 regression: an owned Str passed as a by-value `Str` parameter, then
   stored into a returned struct's field (or returned directly), must be cloned
   so the escaped field/return owns its own buffer (the caller-side temp is
   dropped after the call). Was AOT garbage / JIT lucky-UAF on the old builtin
   `string` cap=-2 borrow ABI; the Str by-value-param-clone path is the analogue.
   Must print the real strings under AOT and stay memcheck-clean. */

import std.core.str

struct S { Str a; int n }

def of(Str s) -> S { return S { a: s, n: 1 } }     // param → struct field → return
def id(Str s) -> Str { return s }                  // param → return directly

struct W { Str label }
methods W { static def make(Str s) -> W { return W { label: s } } }  // static method

def main() -> int {
    S x = of("hello".upper())
    Str y = id("world".upper())
    W w = W.make("widget".upper())

    @print(f"x={x.a} y={y} w={w.label}")
    if x.a.eq?("HELLO") && y.eq?("WORLD") && w.label.eq?("WIDGET") {
        @print("BF045 PASS")
    }
    return 0
}
