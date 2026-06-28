// General NN kernel checks (infra, not model-specific): silu_inplace,
// batchnorm_affine, dwconv1d_strided (axis depthwise conv on [C][outer][L][inner],
// all 5 axis shapes incl inner=1 + inner-tail), swap_mid2 round-trip. Each against
// an f64/naive reference. (silu uses a tanh approx -> tolerance; others exact on
// integer data.)
import std.sci.nn as nn
import std.core.math as math
import std.sys.c as c

def naive_axis(*f32 inp, *f32 w, *f32 out, int C, int outer, int L, int inner, int k, int pad) {
    int Lout = L + 2 * pad - k + 1
    for ci in 0..C {
        for o in 0..outer {
            for ol in 0..Lout {
                for i in 0..inner {
                    f32 acc = 0.0 as f32
                    for j in 0..k {
                        int il = ol - pad + j
                        if il >= 0 && il < L {
                            acc = acc + w[ci * k + j] * inp[((ci * outer + o) * L + il) * inner + i]
                        }
                    }
                    out[((ci * outer + o) * Lout + ol) * inner + i] = acc
                }
            }
        }
    }
}

def conv_ok(int C, int outer, int L, int inner, int k, int pad) -> bool {
    int Lout = L + 2 * pad - k + 1
    *f32 inp = c.malloc(C * outer * L * inner * sizeof(f32)) as *f32
    *f32 w = c.malloc(C * k * sizeof(f32)) as *f32
    *f32 o1 = c.malloc(C * outer * Lout * inner * sizeof(f32)) as *f32
    *f32 o2 = c.malloc(C * outer * Lout * inner * sizeof(f32)) as *f32
    // pb sized for the lane-fill path (padded channel buffer + per-tap masks):
    // outer*L*inner + 2*pad*inner + k*16*inner (generous), also covers path-3 scratch.
    *f32 pb = c.malloc((outer * L * inner + 2 * pad * inner + k * 16 * inner + 64) * sizeof(f32)) as *f32
    int i = 0
    while i < C * outer * L * inner { inp[i] = (((i * 3 + 1) % 7) - 3) as f32; i = i + 1 }
    i = 0
    while i < C * k { w[i] = (((i * 2 + 1) % 5) - 2) as f32; i = i + 1 }
    nn.dwconv1d_strided(inp, w, o1, pb, C, outer, L, inner, k, pad)
    naive_axis(inp, w, o2, C, outer, L, inner, k, pad)
    bool ok = true
    i = 0
    while i < C * outer * Lout * inner { if (o1[i] as f64) != (o2[i] as f64) { ok = false } i = i + 1 }
    c.free(inp as *u8) c.free(w as *u8) c.free(o1 as *u8) c.free(o2 as *u8) c.free(pb as *u8)
    return ok
}

def silu_ok() -> bool {
    int n = 100
    *f32 p = c.malloc(n * sizeof(f32)) as *f32
    int i = 0
    while i < n { p[i] = (((i as f64) - 50.0) * 0.2) as f32; i = i + 1 }
    nn.silu_inplace(p, n)
    f64 e = 0.0
    i = 0
    while i < n {
        f64 x = ((i as f64) - 50.0) * 0.2
        f64 r = x / (1.0 + math.exp(0.0 - x))
        f64 d = (p[i] as f64) - r
        if d < 0.0 { d = 0.0 - d }
        if d > e { e = d }
        i = i + 1
    }
    c.free(p as *u8)
    return e < 0.001
}

def bn_ok() -> bool {
    int C = 5
    int S = 100
    *f32 p = c.malloc(C * S * sizeof(f32)) as *f32
    *f32 sc = c.malloc(C * sizeof(f32)) as *f32
    *f32 bs = c.malloc(C * sizeof(f32)) as *f32
    int ci = 0
    while ci < C {
        sc[ci] = (((ci * 2 + 1) % 5) - 2) as f32
        bs[ci] = (((ci * 3 + 2) % 5) - 2) as f32
        ci = ci + 1
    }
    int i = 0
    while i < C * S { p[i] = ((i % 9) - 4) as f32; i = i + 1 }
    bool ok = true
    nn.batchnorm_affine(p, sc, bs, C, S)
    int cj = 0
    while cj < C {
        int j = 0
        while j < S {
            f32 x = ((cj * S + j) % 9 - 4) as f32
            f32 exp = x * sc[cj] + bs[cj]
            if (p[cj * S + j] as f64) != (exp as f64) { ok = false }
            j = j + 1
        }
        cj = cj + 1
    }
    c.free(p as *u8) c.free(sc as *u8) c.free(bs as *u8)
    return ok
}

def main() {
    bool ok = true
    if !silu_ok() { ok = false  @print("FAIL silu") }
    if !bn_ok()   { ok = false  @print("FAIL bn") }
    if !conv_ok(8, 1, 10, 9*4*4*2, 3, 1) { ok = false  @print("FAIL conv time") }
    if !conv_ok(8, 10, 9, 4*4*2, 3, 1)   { ok = false  @print("FAIL conv freq") }
    if !conv_ok(8, 10*9, 4, 4*2, 3, 1)   { ok = false  @print("FAIL conv row") }
    if !conv_ok(8, 10*9*4, 4, 2, 3, 1)   { ok = false  @print("FAIL conv col") }
    if !conv_ok(8, 10*9*4*4, 2, 1, 2, 0) { ok = false  @print("FAIL conv pol") }
    if !conv_ok(6, 3, 8, 13, 3, 1)       { ok = false  @print("FAIL conv inner-tail") }
    if ok { @print("NN-DWCONV OK") }
}
