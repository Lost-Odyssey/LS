// std.sci.nn.sgemm — blocked FP32 GEMM (register-resident 6×16 micro-kernel)
// checked against a naive triple-loop reference. A and B are filled with small
// integer values, so every C element is an exact f32 integer (|C| < 2^24) and the
// comparison is exact — no tolerance. Shapes exercise the 6×16 bulk, the 1×16 M
// tail, the scalar N tail, and a large 128×256×128 case. Simd is POD; the malloc'd
// buffers are freed, so --memcheck is 0/0/0.

import std.sci.nn as nn
import std.sys.c as sc

def fillA(*f32 A, int M, int K) {
    for i in 0..M {
        for k in 0..K { A[i * K + k] = (((i + 2 * k) % 7) - 3) as f32 }
    }
}

def fillB(*f32 B, int K, int N) {
    for k in 0..K {
        for j in 0..N { B[k * N + j] = (((3 * k + j) % 5) - 2) as f32 }
    }
}

def naive(*f32 A, *f32 B, *f32 C, int M, int N, int K) {
    for i in 0..M {
        for j in 0..N {
            f32 s = 0.0 as f32
            for k in 0..K { s = s + A[i * K + k] * B[k * N + j] }
            C[i * N + j] = s
        }
    }
}

def run_case(int M, int N, int K) -> bool {
    *f32 A = sc.malloc(M * K * sizeof(f32)) as *f32
    *f32 B = sc.malloc(K * N * sizeof(f32)) as *f32
    *f32 C = sc.malloc(M * N * sizeof(f32)) as *f32
    *f32 R = sc.malloc(M * N * sizeof(f32)) as *f32
    fillA(A, M, K)
    fillB(B, K, N)
    naive(A, B, R, M, N, K)
    bool ok = true
    int n = M * N
    // Both GEMM drivers must match the naive reference exactly.
    nn.sgemm(A, B, C, M, N, K)
    int idx = 0
    while idx < n {
        if (C[idx] as f64) != (R[idx] as f64) { ok = false }
        idx = idx + 1
    }
    nn.sgemm_packed(A, B, C, M, N, K)        // BLIS packed/cache-blocked path
    idx = 0
    while idx < n {
        if (C[idx] as f64) != (R[idx] as f64) { ok = false }
        idx = idx + 1
    }
    sc.free(A as *u8)
    sc.free(B as *u8)
    sc.free(C as *u8)
    sc.free(R as *u8)
    return ok
}

def main() {
    bool ok = true
    if !run_case(24, 64, 16) {           // multi-tile bulk (4 M-blocks × 4 N-tiles)
        ok = false
        @print("GEMM FAIL 24x64x16")
    }
    if !run_case(128, 256, 128) {        // a large square-ish case
        ok = false
        @print("GEMM FAIL 128x256x128")
    }
    if !run_case(7, 20, 5) {             // M tail (1) + N scalar tail (4 cols)
        ok = false
        @print("GEMM FAIL 7x20x5")
    }
    if !run_case(6, 16, 1) {            // exactly one 6×16 tile, K=1
        ok = false
        @print("GEMM FAIL 6x16x1")
    }
    if !run_case(3, 48, 17) {           // all M tail (1×16), 3 N tiles
        ok = false
        @print("GEMM FAIL 3x48x17")
    }
    if ok { @print("GEMM OK") }
}
