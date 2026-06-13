// fft_arbitrary_smoke.ls — std.fft Phase 3: arbitrary-N FFT via Bluestein.
// Validates fft() against a naive O(N^2) DFT for prime and composite N (incl. the
// non-power-of-2 cases that exercise Bluestein), plus ifft∘fft round-trip. The
// power-of-2 path (N=8 here) is also re-checked to confirm the dispatch.

import std.str
import std.vec
import std.complex
import std.fft as ft
import math

fn check(bool ok, Str l) { if ok { print(f"ok {l}") } else { print(f"FAIL {l}") } }
fn near(f64 a, f64 b) -> bool { f64 d = a - b; if d < 0.0 { d = 0.0 - d } return d < 0.00001 }

// naive O(N^2) DFT reference: X_k = sum_n x_n exp(-2pi i k n / N)
fn dft_naive(Vec(Complex(f64)) x) -> Vec(Complex(f64)) {
    int n = x.len()
    Vec(Complex(f64)) out = []
    int k = 0
    while k < n {
        Complex(f64) acc = c(f64)(0.0, 0.0)
        int j = 0
        while j < n {
            f64 ang = 0.0 - 2.0 * math.PI * (k as f64) * (j as f64) / (n as f64)
            Complex(f64) w = c(f64)(math.cos(ang), math.sin(ang))
            acc = acc + x.get!(j) * w
            j = j + 1
        }
        out.push(acc)
        k = k + 1
    }
    return out
}

// deterministic pseudo-random-ish complex signal of length n
fn sig(int n) -> Vec(Complex(f64)) {
    Vec(Complex(f64)) v = []
    int i = 0
    while i < n {
        f64 re = math.sin(0.7 * (i as f64) + 1.0) * 3.0
        f64 im = math.cos(0.3 * (i as f64) + 2.0) * 2.0
        v.push(c(f64)(re, im))
        i = i + 1
    }
    return v
}

// compare fft(x) against the naive DFT for length n
fn check_n(int n) {
    Vec(Complex(f64)) x = sig(n)
    Vec(Complex(f64)) fast = ft.fft(x)         // x cloned by value; still usable
    Vec(Complex(f64)) ref = dft_naive(x)
    bool ok = true
    int k = 0
    while k < n {
        Complex(f64) a = fast.get!(k)
        Complex(f64) b = ref.get!(k)
        if !(near(a.re, b.re) && near(a.im, b.im)) { ok = false }
        k = k + 1
    }
    check(ok, f"fft(N={n}) matches naive DFT")

    // round-trip
    Vec(Complex(f64)) back = ft.ifft(fast)
    bool rt = true
    int m = 0
    while m < n {
        Complex(f64) o = x.get!(m)
        Complex(f64) g = back.get!(m)
        if !(near(o.re, g.re) && near(o.im, g.im)) { rt = false }
        m = m + 1
    }
    check(rt, f"ifft(fft(N={n})) round-trip")
}

fn main() {
    check_n(7)      // prime  -> Bluestein
    check_n(11)     // prime  -> Bluestein
    check_n(13)     // prime  -> Bluestein
    check_n(6)      // 2*3    -> mixed-radix
    check_n(9)      // 3*3    -> mixed-radix
    check_n(10)     // 2*5    -> mixed-radix
    check_n(12)     // 4*3    -> mixed-radix
    check_n(15)     // 3*5    -> mixed-radix (radix-3 + radix-5)
    check_n(20)     // 4*5    -> mixed-radix
    check_n(8)      // 2^3    -> radix-2 (dispatch check)
    check_n(1)      // trivial
    print("FFT_ARB PASS")
}
