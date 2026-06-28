// std.sci.nn.conv1d (im2col + sgemm) checked against a naive direct convolution,
// plus an end-to-end conv -> ReLU -> conv block fed entirely from a Pool.
// Integer-valued weights/inputs keep every result an
// exact f32 integer, so comparisons are exact. Buffers are freed → memcheck 0/0/0.

import std.sci.nn as nn
import std.sys.c as c

// reference: direct 1D conv, channel-major [Cin][W] -> [Cout][Wout]
def conv_naive(*f32 input, *f32 weights, *f32 bias, *f32 output,
               int Cin, int Cout, int W, int K, int pad) {
    int Wout = W + 2 * pad - K + 1
    for co in 0..Cout {
        for ow in 0..Wout {
            f32 acc = bias[co]
            for cin in 0..Cin {
                for k in 0..K {
                    int iw = ow - pad + k
                    if iw >= 0 && iw < W {
                        acc = acc + weights[co * Cin * K + cin * K + k] * input[cin * W + iw]
                    }
                }
            }
            output[co * Wout + ow] = acc
        }
    }
}

def relu_naive(*f32 p, int n) {
    for i in 0..n { if p[i] < (0.0 as f32) { p[i] = 0.0 as f32 } }
}

def fill(*f32 p, int n, int a, int b) {
    for i in 0..n { p[i] = (((i * a + b) % 7) - 3) as f32 }   // small ints -3..3
}

def eqbuf(*f32 x, *f32 y, int n) -> bool {
    for i in 0..n { if (x[i] as f64) != (y[i] as f64) { return false } }
    return true
}

def main() {
    bool ok = true

    // ---- single conv1d vs naive ----
    int Cin = 4
    int Cout = 16
    int W = 64
    int K = 3
    int pad = 1
    int Wout = W + 2 * pad - K + 1            // 64
    *f32 inp = c.malloc(Cin * W * sizeof(f32)) as *f32
    *f32 wts = c.malloc(Cout * Cin * K * sizeof(f32)) as *f32
    *f32 bia = c.malloc(Cout * sizeof(f32)) as *f32
    *f32 out = c.malloc(Cout * Wout * sizeof(f32)) as *f32
    *f32 ref = c.malloc(Cout * Wout * sizeof(f32)) as *f32
    *f32 col = c.malloc(Cin * K * Wout * sizeof(f32)) as *f32
    fill(inp, Cin * W, 2, 1)
    fill(wts, Cout * Cin * K, 3, 2)
    fill(bia, Cout, 1, 0)
    nn.conv1d(inp, wts, bia, out, col, Cin, Cout, W, K, pad)
    conv_naive(inp, wts, bia, ref, Cin, Cout, W, K, pad)
    if !eqbuf(out, ref, Cout * Wout) { ok = false  @print("CONV FAIL: single") }

    // direct (im2col-free, register-blocked) conv1d — same result, pbuf scratch
    *f32 outd = c.malloc(Cout * Wout * sizeof(f32)) as *f32
    *f32 pbuf = c.malloc(Cin * (W + 2 * pad) * sizeof(f32)) as *f32
    nn.conv1d_direct(inp, wts, bia, outd, pbuf, Cin, Cout, W, K, pad)
    if !eqbuf(outd, ref, Cout * Wout) { ok = false  @print("CONV FAIL: direct single") }
    c.free(outd as *u8)
    c.free(pbuf as *u8)

    c.free(inp as *u8)
    c.free(wts as *u8)
    c.free(bia as *u8)
    c.free(out as *u8)
    c.free(ref as *u8)
    c.free(col as *u8)

    // direct conv1d at Cout=16 (two 6-blocks + 4 tail) and Wout>=16 (full lanes)
    {
        int dCin = 4
        int dCout = 16
        int dW = 64
        int dK = 3
        int dpad = 1
        int dWout = dW + 2 * dpad - dK + 1
        *f32 di = c.malloc(dCin * dW * sizeof(f32)) as *f32
        *f32 dw = c.malloc(dCout * dCin * dK * sizeof(f32)) as *f32
        *f32 db = c.malloc(dCout * sizeof(f32)) as *f32
        *f32 dod = c.malloc(dCout * dWout * sizeof(f32)) as *f32
        *f32 dref = c.malloc(dCout * dWout * sizeof(f32)) as *f32
        *f32 dpb = c.malloc(dCin * (dW + 2 * dpad) * sizeof(f32)) as *f32
        fill(di, dCin * dW, 2, 1)
        fill(dw, dCout * dCin * dK, 3, 2)
        fill(db, dCout, 1, 0)
        nn.conv1d_direct(di, dw, db, dod, dpb, dCin, dCout, dW, dK, dpad)
        conv_naive(di, dw, db, dref, dCin, dCout, dW, dK, dpad)
        if !eqbuf(dod, dref, dCout * dWout) { ok = false  @print("CONV FAIL: direct block") }
        c.free(di as *u8) c.free(dw as *u8) c.free(db as *u8)
        c.free(dod as *u8) c.free(dref as *u8) c.free(dpb as *u8)
    }

    // ---- end-to-end: conv -> ReLU -> conv, all activations from a Pool ----
    // layer1: 4->16 ch, layer2: 16->4 ch, both K=3 pad=1 over W=24 (Wout=24)
    int w2 = 24
    int c0 = 4
    int c1 = 16
    int c2 = 4
    // persistent weights/inputs (malloc); activations + im2col scratch (Pool)
    *f32 x = c.malloc(c0 * w2 * sizeof(f32)) as *f32
    *f32 w1 = c.malloc(c1 * c0 * K * sizeof(f32)) as *f32
    *f32 b1 = c.malloc(c1 * sizeof(f32)) as *f32
    *f32 w2w = c.malloc(c2 * c1 * K * sizeof(f32)) as *f32
    *f32 b2 = c.malloc(c2 * sizeof(f32)) as *f32
    *f32 refh1 = c.malloc(c1 * w2 * sizeof(f32)) as *f32
    *f32 refh2 = c.malloc(c2 * w2 * sizeof(f32)) as *f32
    fill(x, c0 * w2, 2, 0)
    fill(w1, c1 * c0 * K, 1, 1)
    fill(b1, c1, 1, 0)
    fill(w2w, c2 * c1 * K, 2, 1)
    fill(b2, c2, 1, 0)

    // SIMD path (Pool): col1 + h1 + col2 + h2 carved per "frame"
    nn.Pool p = {}
    p.reserve(8192)
    *f32 col1 = p.tensor(c0 * K * w2)        // 4*3*24 = 288
    *f32 h1 = p.tensor(c1 * w2)              // 16*24 = 384
    *f32 col2 = p.tensor(c1 * K * w2)        // 16*3*24 = 1152
    *f32 h2 = p.tensor(c2 * w2)              // 4*24 = 96
    nn.conv1d(x, w1, b1, h1, col1, c0, c1, w2, K, pad)
    nn.relu_inplace(h1, c1 * w2)
    nn.conv1d(h1, w2w, b2, h2, col2, c1, c2, w2, K, pad)

    // reference chain
    conv_naive(x, w1, b1, refh1, c0, c1, w2, K, pad)
    relu_naive(refh1, c1 * w2)
    conv_naive(refh1, w2w, b2, refh2, c1, c2, w2, K, pad)

    if !eqbuf(h2, refh2, c2 * w2) { ok = false  @print("CONV FAIL: end-to-end") }

    c.free(x as *u8)
    c.free(w1 as *u8)
    c.free(b1 as *u8)
    c.free(w2w as *u8)
    c.free(b2 as *u8)
    c.free(refh1 as *u8)
    c.free(refh2 as *u8)

    if ok { @print("CONV OK") }
}
