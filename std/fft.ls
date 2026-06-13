// std/fft.ls — Fast Fourier Transform over Complex(f64).
//
// Phase 2: radix-2 Cooley-Tukey (decimation-in-time), N must be a power of 2.
// Mixed-radix (3/4/5) + Bluestein (arbitrary N) come in Phase 3; multi-dim in
// Phase 5. The whole module is NON-generic (every value is Complex(f64)) — FFT is
// only meaningful for floats, so there is no type parameter and internal helpers
// call each other freely.
//
//   fft(Vec(Complex(f64)))  -> Vec(Complex(f64))   forward, unscaled
//   ifft(Vec(Complex(f64))) -> Vec(Complex(f64))   inverse, scaled by 1/N (NumPy)
//
// Convention: forward uses W_N^k = exp(-2*pi*i*k/N); inverse uses +sign and 1/N.

import std.vec
import std.complex
import math

// n is a power of two (n >= 1)
fn _is_pow2(int n) -> bool { return n > 0 && (n & (n - 1)) == 0 }

// In-place bit-reversal permutation of `a` (length n = 2^k).
fn _bitrev(&!Vec(Complex(f64)) a, int n) {
    int j = 0
    int i = 1
    while i < n {
        int bit = n >> 1
        // `while (expr) != x {` mis-parses (std.map M-0 gotcha) — use a temp.
        int masked = j & bit
        while masked != 0 {
            j = j ^ bit            // clear the set high bit
            bit = bit >> 1
            masked = j & bit
        }
        j = j | bit
        if i < j {
            Complex(f64) tmp = a.get!(i)
            a.set!(i, a.get!(j))
            a.set!(j, tmp)
        }
        i = i + 1
    }
}

// Iterative DIT butterflies over a bit-reversed `a`. `sign` = -1 forward, +1 inverse.
fn _butterfly(&!Vec(Complex(f64)) a, int n, f64 sign) {
    int len = 2
    while len <= n {
        // twiddle step wlen = exp(sign * 2*pi*i / len)
        f64 ang = sign * 2.0 * math.PI / (len as f64)
        Complex(f64) wlen = c(f64)(math.cos(ang), math.sin(ang))
        int half = len / 2
        int base = 0
        while base < n {
            Complex(f64) w = c(f64)(1.0, 0.0)
            int j = 0
            while j < half {
                Complex(f64) u = a.get!(base + j)
                Complex(f64) t = a.get!(base + j + half) * w
                a.set!(base + j, u + t)
                a.set!(base + j + half, u - t)
                w = w * wlen
                j = j + 1
            }
            base = base + len
        }
        len = len * 2
    }
}

// Forward FFT (unscaled). Input taken by value (cloned); the result is a new Vec.
fn fft(Vec(Complex(f64)) x) -> Vec(Complex(f64)) {
    int n = x.len()
    if !_is_pow2(n) {
        print("fft: length must be a power of 2")
        std.c.abort()
    }
    _bitrev(&!x, n)
    _butterfly(&!x, n, 0.0 - 1.0)
    return x
}

// Inverse FFT, scaled by 1/N (NumPy convention).
fn ifft(Vec(Complex(f64)) x) -> Vec(Complex(f64)) {
    int n = x.len()
    if !_is_pow2(n) {
        print("ifft: length must be a power of 2")
        std.c.abort()
    }
    _bitrev(&!x, n)
    _butterfly(&!x, n, 1.0)
    f64 inv = 1.0 / (n as f64)
    int i = 0
    while i < n {
        x.set!(i, x.get!(i).scale(inv))
        i = i + 1
    }
    return x
}
