// fft_nd_smoke.ls — std.sci.fft Phase 5: multi-dimensional FFT (fft2/fftn) over
// Tensor(Complex(f64)). Validates fft2 against a naive 2D DFT (3x4: axis-0 length 3
// hits Bluestein, axis-1 length 4 hits radix-2) and ifft2(fft2(x)) round-trip.

import std.core.str
import std.core.vec
import std.sci.complex
import std.sci.tensor
import std.sci.fft as ft
import std.core.math as math

def check(bool ok, Str l) { if ok { @print(f"ok {l}") } else { @print(f"FAIL {l}") } }
def near(f64 a, f64 b) -> bool { f64 d = a - b; if d < 0.0 { d = 0.0 - d } return d < 0.00001 }

// naive O(N1^2 N2^2) 2D DFT, result flattened row-major (k1*N2 + k2)
def dft2_naive(Tensor(Complex(f64)) x, int n1, int n2) -> Vec(Complex(f64)) {
    Vec(Complex(f64)) out = []
    int k1 = 0
    while k1 < n1 {
        int k2 = 0
        while k2 < n2 {
            Complex(f64) acc = c(f64)(0.0, 0.0)
            int m1 = 0
            while m1 < n1 {
                int m2 = 0
                while m2 < n2 {
                    f64 ph = ((k1 * m1) as f64) / (n1 as f64) + ((k2 * m2) as f64) / (n2 as f64)
                    f64 ang = 0.0 - 2.0 * math.PI * ph
                    Complex(f64) w = c(f64)(math.cos(ang), math.sin(ang))
                    acc = acc + x.at2(m1, m2) * w
                    m2 = m2 + 1
                }
                m1 = m1 + 1
            }
            out.push(acc)
            k2 = k2 + 1
        }
        k1 = k1 + 1
    }
    return out
}

def main() {
    int n1 = 3
    int n2 = 4
    Vec(int) sh = [3, 4]

    // deterministic complex signal
    Vec(Complex(f64)) flat = []
    int i = 0
    while i < n1 * n2 {
        f64 re = math.sin(0.5 * (i as f64) + 1.0) * 2.0
        f64 im = math.cos(0.4 * (i as f64) + 0.5)
        flat.push(c(f64)(re, im))
        i = i + 1
    }
    Tensor(Complex(f64)) x = {}
    x.init_from(sh, flat)

    // fft2 vs naive 2D DFT
    Vec(Complex(f64)) ref = dft2_naive(x, n1, n2)
    Tensor(Complex(f64)) Y = ft.fft2(x)
    bool m_ok = true
    int k = 0
    while k < n1 * n2 {
        Complex(f64) a = Y.get!(k)
        Complex(f64) b = ref.get!(k)
        if !(near(a.re, b.re) && near(a.im, b.im)) { m_ok = false }
        k = k + 1
    }
    check(m_ok, "fft2(3x4) matches naive 2D DFT")

    // round-trip ifft2(fft2(x)) == x
    Tensor(Complex(f64)) back = ft.ifft2(Y)
    bool rt = true
    int p = 0
    while p < n1 * n2 {
        Complex(f64) o = x.get!(p)
        Complex(f64) g = back.get!(p)
        if !(near(o.re, g.re) && near(o.im, g.im)) { rt = false }
        p = p + 1
    }
    check(rt, "ifft2(fft2(x)) round-trip")

    @print("FFT_ND PASS")
}
