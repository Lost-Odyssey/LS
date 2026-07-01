// nn.softmax_rows — vectorized (smd.exp) row-softmax checked per-element against
// an f64 scalar reference, plus row-sum == 1. Covers cols = 16 (pure SIMD),
// 10 (scalar tail only), 20 (16 + tail), 64 (4 chunks).
import std.sci.nn as nn
import std.core.math as math
import std.sys.c as sc

def approx(f64 a, f64 b, f64 tol) -> bool {
    f64 d = a - b
    if d < 0.0 { d = 0.0 - d }
    return d < tol
}

def check(int rows, int cols) -> bool {
    *f32 p = sc.malloc(rows * cols * sizeof(f32)) as *f32
    *f32 q = sc.malloc(rows * cols * sizeof(f32)) as *f32
    for i in 0..rows {
        for j in 0..cols {
            f32 v = (((((i * 7 + j * 3) % 17) as f32) * 0.3) - 2.0) as f32
            p[i * cols + j] = v
            q[i * cols + j] = v
        }
    }
    nn.softmax_rows(p, rows, cols)
    bool ok = true
    for i in 0..rows {
        f64 mx = q[i * cols] as f64
        for j in 1..cols { f64 vv = q[i * cols + j] as f64; if vv > mx { mx = vv } }
        f64 s = 0.0
        for j in 0..cols { s = s + math.exp((q[i * cols + j] as f64) - mx) }
        f64 rowsum = 0.0
        for j in 0..cols {
            f64 ref = math.exp((q[i * cols + j] as f64) - mx) / s
            f64 got = p[i * cols + j] as f64
            rowsum = rowsum + got
            if !approx(got, ref, 0.002) { ok = false }
        }
        if !approx(rowsum, 1.0, 0.001) { ok = false }
    }
    sc.free(p as *u8); sc.free(q as *u8)
    return ok
}

def main() {
    bool ok = true
    if !check(4, 16) { ok = false; @print("cols=16 FAIL\n") }
    if !check(3, 10) { ok = false; @print("cols=10 FAIL\n") }
    if !check(2, 20) { ok = false; @print("cols=20 FAIL\n") }
    if !check(5, 64) { ok = false; @print("cols=64 FAIL\n") }
    if ok { @print("SOFTMAX OK\n") } else { @print("SOFTMAX FAIL\n") }
}
