// zero_trait_smoke.ls — Phase 0 of std.complex/std.fft: the compiler foundation
// for `T.zero()`. Exercises static trait methods (`trait Zero { static fn zero }`)
// and static-method dispatch on a generic type parameter inside a monomorphized
// generic method body. No stdlib imports beyond str/vec — the trait is inline so
// this isolates the compiler mechanism. JIT + AOT + memcheck.

import std.str

trait Zero { static fn zero() -> Self }
impl Zero for int { static fn zero() -> Self { return 0 } }
impl Zero for f64 { static fn zero() -> Self { return 0.0 } }

struct Box(T) { T v }
impl(T) Box(T) {
    // `T.zero()`: static trait method dispatched on the type parameter.
    fn z(&self) -> T { return T.zero() }
    // also exercise the field-store path through T.zero()
    fn reset(&!self) { self.v = T.zero() }
    fn get(&self) -> T { return self.v }
}

fn check(bool ok, Str l) { if ok { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn main() {
    Box(int) bi = { v: 7 }
    check(bi.z() == 0, "int T.zero() == 0")
    check(bi.get() == 7, "int field before reset")
    bi.reset()
    check(bi.get() == 0, "int field after reset == 0")

    Box(f64) bf = { v: 3.5 }
    check(bf.z() == 0.0, "f64 T.zero() == 0.0")
    bf.reset()
    check(bf.get() == 0.0, "f64 field after reset == 0.0")

    print("ZERO_TRAIT PASS")
}
