// Phase 2: f16 / bf16 SIMD element types. f16 arithmetic is allowed (native
// under +avx512fp16, promoted to f32 otherwise); bf16 is storage + convert only
// (arithmetic rejected). __simd_cast bridges precisions. Simd is POD -> memcheck
// 0/0/0 (the malloc'd buffer is freed).

import std.sys.c as sc

def check(bool ok, Str name) {
    if !ok { @print(f"F16 FAIL: {name}") }
}

def main() {
    // ---- f16 arithmetic (promoted to f32 on AVX2; native vaddph on GNR) ----
    Simd(f16, 16) a = __simd_splat(2.0)
    Simd(f16, 16) b = __simd_splat(3.0)
    Simd(f16, 16) c = a + b                       // 5.0
    f16 l = __simd_lane(c, 0)
    check((l as f64) == 5.0, "f16 add+lane")
    @print(l)                                     // print(f16) -> 5.000000

    // ---- mixed precision: f16 -> f32 compute -> f16 (the AI inference pattern) ----
    Simd(f16, 16) h = __simd_splat(1.5)
    Simd(f32, 16) f = __simd_cast(h)              // widen f16 -> f32
    Simd(f32, 16) g = f * f                        // 2.25 in f32
    Simd(f16, 16) back = __simd_cast(g)           // narrow f32 -> f16
    check((__simd_lane(back, 0) as f64) == 2.25, "f16<->f32 cast roundtrip")
    f32 fred = __simd_reduce_add(g)               // 2.25 * 16 = 36
    check((fred as f64) == 36.0, "f32 reduce after cast")

    // ---- f16 load/store ----
    *f16 buf = sc.malloc(16 * sizeof(f16)) as *f16
    __simd_store(buf, 0, c)                        // 5.0 x16
    Simd(f16, 16) r = __simd_load(buf, 0)
    check((__simd_lane(r, 3) as f64) == 5.0, "f16 store/load")
    sc.free(buf as *u8)

    // (bf16 element type exists + arithmetic is rejected, but bf16<->f32 runtime
    // conversion needs +avx512bf16 or a soft-float path — deferred to Phase 2b.)

    @print("F16 OK")
}
