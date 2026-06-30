// generic_freefn_in_body.ls — regression for the generic-free-function call-site
// mangling gap (docs/plan_generic_freefn_mangle.md).
//
// A generic free function called with an ABSTRACT type param `T` from inside
// another generic body (method or free function) used to emit `make(T)` at
// codegen (no alias context) → "undefined function 'make(T)'". The fix stashes
// the checker-resolved concrete type names on the call node and makes codegen
// prefer them. Covers: abstract T from a generic method body, concrete type from
// a generic method body, abstract T from a generic free-function body, and a
// generic free function that constructs+returns a generic container by abstract T.
//
// Prints "ok <label>" / "FAIL <label>", then "GFN PASS".

import std.core.str
import std.core.vec

def identity(T)(T x) -> T { return x }

// A generic free function that builds a generic container parameterized by the
// caller's abstract T — the new_set(T)() shape that std.core.set hit.
def singleton(T)(T x) -> Vec(T) {
    Vec(T) v = {}
    v.push(x)
    return v
}

// Generic free function whose body calls another generic free function with its
// OWN abstract T (generic-in-generic, free-function flavor).
def twice(T)(T x) -> T { return identity(T)(x) }

struct Box(T) { T v }

methods(T) Box(T) {
    // abstract T passed to a generic free function from a generic method body
    def echo(&self) -> T { return identity(T)(self.v) }
    // concrete type arg from a generic method body (the path that already worked)
    def seven(&self) -> int { return identity(int)(7) }
    // generic free fn returning a generic container, by abstract T
    def wrap(&self) -> Vec(T) { return singleton(T)(self.v) }
}

def check(bool cond, Str label) {
    if cond { @print(f"ok {label}") } else { @print(f"FAIL {label}") }
}

def main() {
    Box(int) bi = Box(int){ v: 42 }
    check(bi.seven() == 7, "concrete arg in method body")
    check(bi.echo() == 42, "abstract T in method body (int)")
    check(twice(int)(5) == 5, "abstract T in free-fn body (int)")

    Vec(int) wi = bi.wrap()
    check(wi.len() == 1, "container-returning free fn (int) len")
    check(wi[0] == 42, "container-returning free fn (int) value")

    // has_drop element: same paths must keep ownership correct.
    Box(Str) bs = Box(Str){ v: "alpha" }
    Str e = bs.echo()
    check(e.eq?("alpha"), "abstract T in method body (Str)")
    Vec(Str) ws = bs.wrap()
    check(ws.len() == 1, "container-returning free fn (Str) len")
    check(ws[0].eq?("alpha"), "container-returning free fn (Str) value")

    @print("GFN PASS")
}
