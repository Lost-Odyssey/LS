// fft_real_dct_smoke.ls — std.fft Phase 4: rfft/irfft + DCT-II/III.
// rfft validated against the full complex FFT; irfft round-trip; dct known value
// ([1,1,1,1] -> [8,0,0,0]) + idct(dct(x)) round-trip identity.

import std.str
import std.vec
import std.complex
import std.fft as ft
import math

fn check(bool ok, Str l) { if ok { print(f"ok {l}") } else { print(f"FAIL {l}") } }
fn near(f64 a, f64 b) -> bool { f64 d = a - b; if d < 0.0 { d = 0.0 - d } return d < 0.00001 }

fn rsig(int n) -> Vec(f64) {
    Vec(f64) v = []
    int i = 0
    while i < n { v.push(math.sin(0.6 * (i as f64) + 0.4) * 2.0 + 0.5); i = i + 1 }
    return v
}

fn cplxify(Vec(f64) r) -> Vec(Complex(f64)) {
    Vec(Complex(f64)) v = []
    int i = 0
    while i < r.len() { v.push(c(f64)(r.get!(i), 0.0)); i = i + 1 }
    return v
}

// naive O(N^2) DCT-II reference: y_k = 2 sum_n x_n cos(pi(2n+1)k/(2N))
fn dct_naive(Vec(f64) x) -> Vec(f64) {
    int bigN = x.len()
    f64 d = 2.0 * (bigN as f64)
    Vec(f64) y = []
    int k = 0
    while k < bigN {
        f64 s = 0.0
        int n = 0
        while n < bigN {
            s = s + x.get!(n) * math.cos(math.PI * ((2 * n + 1) as f64) * (k as f64) / d)
            n = n + 1
        }
        y.push(2.0 * s)
        k = k + 1
    }
    return y
}

// compare rfft(x) against full fft(complex(x))[0..n/2] for length n
fn check_rfft(int n) {
    Vec(f64) x = rsig(n)
    Vec(Complex(f64)) R = ft.rfft(x)
    Vec(Complex(f64)) F = ft.fft(cplxify(x))
    bool ok = R.len() == n / 2 + 1
    int k = 0
    while k < n / 2 + 1 {
        Complex(f64) a = R.get!(k)
        Complex(f64) b = F.get!(k)
        if !(near(a.re, b.re) && near(a.im, b.im)) { ok = false }
        k = k + 1
    }
    check(ok, f"rfft(N={n}) matches fft front half")
    // round-trip
    Vec(f64) xr = ft.irfft(R, n)
    bool rt = true
    int i = 0
    while i < n {
        if !near(xr.get!(i), x.get!(i)) { rt = false }
        i = i + 1
    }
    check(rt, f"irfft(rfft(N={n})) round-trip")
}

// compare Makhoul dct against the naive DCT for length n
fn check_dct(int n) {
    Vec(f64) x = rsig(n)
    Vec(f64) fast = ft.dct(x)
    Vec(f64) ref = dct_naive(x)
    bool ok = true
    int k = 0
    while k < n {
        if !near(fast.get!(k), ref.get!(k)) { ok = false }
        k = k + 1
    }
    check(ok, f"dct(N={n}) matches naive DCT")
}

fn main() {
    // rfft(x) == fft(complex(x))[0 .. N/2]  (N=8)
    Vec(f64) x = rsig(8)
    Vec(Complex(f64)) R = ft.rfft(x)
    Vec(Complex(f64)) F = ft.fft(cplxify(x))
    check(R.len() == 5, "rfft len = N/2+1 = 5")
    bool m_ok = true
    int k = 0
    while k < 5 {
        Complex(f64) a = R.get!(k)
        Complex(f64) b = F.get!(k)
        if !(near(a.re, b.re) && near(a.im, b.im)) { m_ok = false }
        k = k + 1
    }
    check(m_ok, "rfft matches fft front half")

    // irfft(rfft(x)) == x  (even N=8)
    Vec(f64) xr = ft.irfft(R, 8)
    bool rt8 = true
    int i = 0
    while i < 8 {
        if !near(xr.get!(i), x.get!(i)) { rt8 = false }
        i = i + 1
    }
    check(rt8, "irfft(rfft) round-trip N=8")

    // odd N=7 round-trip too
    Vec(f64) x7 = rsig(7)
    Vec(f64) xr7 = ft.irfft(ft.rfft(x7), 7)
    bool rt7 = true
    int j = 0
    while j < 7 {
        if !near(xr7.get!(j), x7.get!(j)) { rt7 = false }
        j = j + 1
    }
    check(rt7, "irfft(rfft) round-trip N=7")

    // DCT-II known value: dct([1,1,1,1]) = [8,0,0,0]
    Vec(f64) ones = [1.0, 1.0, 1.0, 1.0]
    Vec(f64) d = ft.dct(ones)
    check(near(d.get!(0), 8.0) && near(d.get!(1), 0.0) &&
          near(d.get!(2), 0.0) && near(d.get!(3), 0.0), "dct([1,1,1,1])=[8,0,0,0]")

    // idct(dct(x)) == x  (N=6)
    Vec(f64) s = rsig(6)
    Vec(f64) sr = ft.idct(ft.dct(s))
    bool dctrt = true
    int m = 0
    while m < 6 {
        if !near(sr.get!(m), s.get!(m)) { dctrt = false }
        m = m + 1
    }
    check(dctrt, "idct(dct(x)) round-trip N=6")

    // packed rfft (even N, incl. non-pow2 -> half-length Bluestein) + odd fallback
    check_rfft(6)
    check_rfft(10)
    check_rfft(12)
    check_rfft(9)

    // Makhoul dct matches naive DCT across sizes (incl. odd N)
    check_dct(5)
    check_dct(6)
    check_dct(7)
    check_dct(8)

    print("FFT_REAL_DCT PASS")
}
