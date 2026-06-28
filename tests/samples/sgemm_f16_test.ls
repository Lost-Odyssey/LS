// Verify nn.sgemm_f16 (f16 storage, f32 accumulate) against an f32 naive ref.
// Small integer data is exact in f16, so f32-accumulated results match exactly.
import std.sci.nn as nn
import std.sci.simd as smd
import std.sys.c as c

// vector f32 -> f16 conversion (AOT-clean: vcvtps2ph, no scalar libcall)
def to_f16(*f32 src, *f16 dst, int n) {
    int i = 0
    while i + 16 <= n {
        Simd(f32, 16) v = __simd_load(src, i)
        Simd(f16, 16) h = __simd_cast(v)
        __simd_store(dst, i, h)
        i = i + 16
    }
    int rem = n - i
    if rem > 0 {
        Simd(f32, 16) v = __simd_load_masked(src, i, rem)
        Simd(f16, 16) h = __simd_cast(v)
        __simd_store_masked(dst, i, h, rem)
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

def run(int M, int N, int K) -> bool {
    *f16 Ah = c.malloc(M * K * sizeof(f16)) as *f16
    *f16 Bh = c.malloc(K * N * sizeof(f16)) as *f16
    *f32 Af = c.malloc(M * K * sizeof(f32)) as *f32
    *f32 Bf = c.malloc(K * N * sizeof(f32)) as *f32
    *f32 Cr = c.malloc(M * N * sizeof(f32)) as *f32
    *f32 Cg = c.malloc(M * N * sizeof(f32)) as *f32
    int i = 0
    while i < M * K { Af[i] = (((i * 3 + 1) % 7) - 3) as f32; i = i + 1 }
    i = 0
    while i < K * N { Bf[i] = (((i * 2 + 5) % 7) - 3) as f32; i = i + 1 }
    to_f16(Af, Ah, M * K)
    to_f16(Bf, Bh, K * N)
    nn.sgemm_f16(Ah, Bh, Cg, M, N, K)
    naive(Af, Bf, Cr, M, N, K)
    bool ok = true
    i = 0
    while i < M * N { if (Cg[i] as f64) != (Cr[i] as f64) { ok = false } i = i + 1 }
    c.free(Ah as *u8) c.free(Bh as *u8) c.free(Af as *u8)
    c.free(Bf as *u8) c.free(Cr as *u8) c.free(Cg as *u8)
    return ok
}

def main() {
    bool ok = true
    if !run(16, 64, 64) { ok = false  @print("FAIL 16x64x64") }   // M>=6 blocks, N mult16
    if !run(16, 256, 64) { ok = false  @print("FAIL 16x256x64") } // wide N
    if !run(7, 20, 9) { ok = false  @print("FAIL 7x20x9") }       // M tail + N tail (20)
    if !run(1, 10, 4) { ok = false  @print("FAIL 1x10x4") }       // M=1, N<16 all scalar
    if !run(13, 48, 32) { ok = false  @print("FAIL 13x48x32") }   // M tail row
    if ok { @print("SGEMM-F16 OK") }
}
