// fft_smoke.ls — std.sci.fft Phase 2: radix-2 FFT/ifft (N = 2^k). Known DFT values,
// impulse response, and fft∘ifft round-trip identity. JIT + AOT + memcheck.

import std.core.str
import std.core.vec
import std.sci.complex
import std.sci.fft as ft
import std.core.math as math

def check(bool ok, Str l) { if ok { @print(f"ok {l}") } else { @print(f"FAIL {l}") } }
def near(f64 a, f64 b) -> bool { f64 d = a - b; if d < 0.0 { d = 0.0 - d } return d < 0.00001 }

// real signal -> Vec(Complex(f64))
def rvec(Vec(f64) xs) -> Vec(Complex(f64)) {
    Vec(Complex(f64)) v = []
    int i = 0
    while i < xs.len() {
        v.push(c(f64)(xs.get!(i), 0.0))
        i = i + 1
    }
    return v
}

def main() {
    // DFT of [1,2,3,4] with W^k = exp(-2pi i k n / N):
    //   X = [10, -2+2i, -2, -2-2i]
    Vec(f64) xr = [1.0, 2.0, 3.0, 4.0]
    Vec(Complex(f64)) x = rvec(xr)
    Vec(Complex(f64)) X = ft.fft(x)
    Complex(f64) x0 = X.get!(0)
    Complex(f64) x1 = X.get!(1)
    Complex(f64) x2 = X.get!(2)
    Complex(f64) x3 = X.get!(3)
    check(near(x0.re, 10.0) && near(x0.im, 0.0), "X[0]=10")
    check(near(x1.re, -2.0) && near(x1.im, 2.0), "X[1]=-2+2i")
    check(near(x2.re, -2.0) && near(x2.im, 0.0), "X[2]=-2")
    check(near(x3.re, -2.0) && near(x3.im, -2.0), "X[3]=-2-2i")

    // impulse [1,0,0,0,0,0,0,0] (N=8) -> all ones
    Vec(f64) impr = [1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    Vec(Complex(f64)) imp = rvec(impr)
    Vec(Complex(f64)) IMP = ft.fft(imp)
    bool all_one = true
    int k = 0
    while k < 8 {
        Complex(f64) e = IMP.get!(k)
        if !(near(e.re, 1.0) && near(e.im, 0.0)) { all_one = false }
        k = k + 1
    }
    check(all_one, "impulse -> all ones (N=8)")

    // round-trip: ft.ifft(ft.fft(y)) == y  (N=8, mixed re/im)
    Vec(Complex(f64)) y = []
    y.push(c(f64)(1.0, -1.0))
    y.push(c(f64)(2.5, 0.5))
    y.push(c(f64)(-3.0, 2.0))
    y.push(c(f64)(0.0, 4.0))
    y.push(c(f64)(7.0, -2.0))
    y.push(c(f64)(-1.5, 1.5))
    y.push(c(f64)(3.0, 3.0))
    y.push(c(f64)(-2.0, -0.5))
    Vec(Complex(f64)) Y = ft.fft(y)            // y cloned by value; y still usable
    Vec(Complex(f64)) yr = ft.ifft(Y)
    bool rt = true
    int m = 0
    while m < 8 {
        Complex(f64) orig = y.get!(m)
        Complex(f64) got = yr.get!(m)
        if !(near(orig.re, got.re) && near(orig.im, got.im)) { rt = false }
        m = m + 1
    }
    check(rt, "ft.ifft(ft.fft(y)) == y roundtrip")

    @print("FFT PASS")
}
