// zero_trait_smoke.ls — Phase 0 of std.sci.complex/std.sci.fft: the compiler foundation
// for `T.zero()`. Exercises static trait methods (`trait Zero { static def zero }`)
// and static-method dispatch on a generic type parameter inside a monomorphized
// generic method body. No stdlib imports beyond str/vec — the trait is inline so
// this isolates the compiler mechanism. JIT + AOT + memcheck.

import std.core.str

interface Zero { static def zero() -> Self }
methods int: Zero { static def zero() -> Self { return 0 } }
methods f64: Zero { static def zero() -> Self { return 0.0 } }

struct Box(T) { T v }
methods(T) Box(T) {
    // `T.zero()`: static trait method dispatched on the type parameter.
    def z(&self) -> T { return T.zero() }
    // also exercise the field-store path through T.zero()
    def reset(&!self) { self.v = T.zero() }
    def get(&self) -> T { return self.v }
}

def check(bool ok, Str l) { if ok { @print(f"ok {l}") } else { @print(f"FAIL {l}") } }

def main() {
    Box(int) bi = { v: 7 }
    check(bi.z() == 0, "int T.zero() == 0")
    check(bi.get() == 7, "int field before reset")
    bi.reset()
    check(bi.get() == 0, "int field after reset == 0")

    Box(f64) bf = { v: 3.5 }
    check(bf.z() == 0.0, "f64 T.zero() == 0.0")
    bf.reset()
    check(bf.get() == 0.0, "f64 field after reset == 0.0")

    @print("ZERO_TRAIT PASS")
}
