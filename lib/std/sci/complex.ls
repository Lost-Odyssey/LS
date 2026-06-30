// std/complex.ls — generic complex number Complex(T).
//
// A POD value type `Complex(T) { T re; T im }`, generic over the scalar element
// (f64 for FFT/DSP, also f32/int for exact Gaussian-integer arithmetic). After
// monomorphization Complex(f64) is byte-identical to a hand-written {f64,f64} —
// zero overhead. Pure LS, no builtin support.
//
// Operators (+ - * /) are real operator overloads via generic trait impls
// (impl(T) Add for Complex(T), ...), so `c1 * c2` works naturally. `zero()`/`one()`
// are static methods, so `Complex(f64).zero()` and (inside a generic body) `T.zero()`
// both resolve here — this is what lets `Tensor(Complex(f64))` reductions work.
//
// Float-only methods (abs/arg/exp/from_polar) use math.* and are only valid for
// float T; they instantiate lazily, so Complex(int) is fine as long as you don't
// call them.
//
// NOTE: the inherent `impl(T) Complex(T)` block MUST precede the trait impls — the
// generic trait-impl methods are folded into it for monomorphization.

import std.core.num
import std.core.math as math

struct Complex(T) { T re; T im }

methods Complex(T) {
    // additive / multiplicative identities (static — used as Complex(f64).zero()
    // and, inside generic bodies, as T.zero())
    static def zero() -> Complex(T) { return {re: 0 as T, im: 0 as T} }
    static def one()  -> Complex(T) { return {re: 1 as T, im: 0 as T} }
    // imaginary unit i
    static def i_unit() -> Complex(T) { return {re: 0 as T, im: 1 as T} }

    def conj(&self) -> Complex(T) { return {re: self.re, im: 0 as T - self.im} }
    // squared magnitude re^2 + im^2 (no sqrt; valid for any numeric T)
    def norm(&self) -> T { return self.re * self.re + self.im * self.im }
    // scalar multiply
    def scale(&self, T s) -> Complex(T) { return {re: self.re * s, im: self.im * s} }

    // --- float-only (math.*) — instantiate only for float T ---
    def abs(&self) -> f64 { return math.sqrt(self.norm()) }
    def arg(&self) -> f64 { return math.atan2(self.im, self.re) }
    // e^z = e^re * (cos im + i sin im)
    def exp(&self) -> Complex(T) {
        T er = math.exp(self.re)
        return {re: er * math.cos(self.im), im: er * math.sin(self.im)}
    }
    static def from_polar(T r, T theta) -> Complex(T) {
        return {re: r * math.cos(theta), im: r * math.sin(theta)}
    }
}

// --- operator overloads (generic trait impls, folded into the inherent impl) ---

methods Complex(T): Add {
    def +(&self, &Complex(T) rhs) -> Complex(T) {
        return {re: self.re + rhs.re, im: self.im + rhs.im}
    }
}

methods Complex(T): Sub {
    def -(&self, &Complex(T) rhs) -> Complex(T) {
        return {re: self.re - rhs.re, im: self.im - rhs.im}
    }
}

methods Complex(T): Mul {
    // (a+bi)(c+di) = (ac - bd) + (ad + bc)i
    def *(&self, &Complex(T) rhs) -> Complex(T) {
        return {re: self.re * rhs.re - self.im * rhs.im,
                im: self.re * rhs.im + self.im * rhs.re}
    }
}

methods Complex(T): Div {
    // (a+bi)/(c+di) = ((ac+bd) + (bc-ad)i) / (c^2 + d^2)   (truncates for int T)
    def /(&self, &Complex(T) rhs) -> Complex(T) {
        T d = rhs.re * rhs.re + rhs.im * rhs.im
        return {re: (self.re * rhs.re + self.im * rhs.im) / d,
                im: (self.im * rhs.re - self.re * rhs.im) / d}
    }
}

// --- free-function constructors (generic, bare-name: `c(f64)(re, im)`) ---
//
// These are free fns because a static method on a generic instance cannot be
// called directly (`Complex(f64).zero()` does not parse — static dispatch only
// works on a type PARAMETER, e.g. `T.zero()` inside a generic body). The static
// methods above remain for that generic-dispatch role; users construct via these.

// rectangular: c(re, im)
def c(T)(T re, T im) -> Complex(T) { return {re: re, im: im} }
// real axis: real(x) = x + 0i
def real(T)(T re) -> Complex(T) { return {re: re, im: 0 as T} }
// polar: from_polar(r, theta) = r*(cos theta + i sin theta)  (float-only)
def from_polar(T)(T r, T theta) -> Complex(T) {
    return {re: r * math.cos(theta), im: r * math.sin(theta)}
}
