// std.sci.nn.Pool — static f32 activation arena: one backing malloc, 64-byte
// aligned tensors carved per frame, O(1) reset, abort-on-overflow. The hard-real-
// time memory model for fixed-shape inference. Single backing block freed by the
// destructor, so --memcheck is 0/0/0.

import std.sci.nn as nn

def check(bool ok, Str name) {
    if !ok { @print(f"POOL FAIL: {name}") }
}

def main() {
    nn.Pool p = {}
    p.reserve(1024)                                  // 1024 f32 = 4096 bytes

    *f32 a = p.tensor(16)
    *f32 b = p.tensor(16)
    check((a as i64) % 64 == 0, "tensor a 64-aligned")
    check((b as i64) % 64 == 0, "tensor b 64-aligned")
    check((b as i64) - (a as i64) == 64, "consecutive tensors 64 apart")

    // write/read through the carved buffer
    for i in 0..16 { a[i] = (i + 1) as f32 }         // 1..16
    Simd(f32, 16) va = __simd_load(a, 0)
    check((__simd_reduce_add(va) as f64) == 136.0, "sum 1..16")   // 16*17/2

    // ReLU(-va) into b — exercises a kernel writing pool memory
    Simd(f32, 16) z = __simd_zero()
    Simd(f32, 16) neg = z - va
    __simd_store(b, 0, __simd_max(neg, z))           // all <=0 -> 0
    check((b[0] as f64) == 0.0, "relu wrote pool buffer")

    // planning: high-water mark = 16 + 16, each aligned to 64B (=16 f32) → 32 f32
    check(p.used_floats() == 32, "used_floats high-water")

    // O(1) reset reuses the same block
    p.reset()
    check(p.used_floats() == 0, "reset rewinds")
    *f32 c2 = p.tensor(16)
    check((c2 as i64) == (a as i64), "reset reuses the first slot")

    // feed a GEMM entirely from the pool (zero per-call malloc)
    p.reset()
    *f32 mA = p.tensor(2 * 3)                         // 2x3
    *f32 mB = p.tensor(3 * 16)                        // 3x16
    *f32 mC = p.tensor(2 * 16)                        // 2x16
    for i in 0..6  { mA[i] = 1.0 as f32 }
    for i in 0..48 { mB[i] = 2.0 as f32 }
    nn.sgemm(mA, mB, mC, 2, 16, 3)                    // each C = sum_k 1*2 = 6
    check((mC[0] as f64) == 6.0, "gemm from pool C[0]")
    check((mC[31] as f64) == 6.0, "gemm from pool C[last]")

    @print("POOL OK")
}
