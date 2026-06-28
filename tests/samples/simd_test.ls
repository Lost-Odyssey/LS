// Phase 1: Simd(T, N) portable SIMD vector — type + core intrinsics + operators
// + memory load/store. Single-threaded; Simd is POD so --memcheck is 0/0/0
// (the malloc'd buffer is freed, so the heap stays balanced too).

import std.sys.c as sc

def check(bool ok, Str name) {
    if !ok { @print(f"SIMD FAIL: {name}") }
}

def main() {
    // ---- f64 (512-bit lane; on AVX2 the backend splits to ymm, still correct) ----
    Simd(f64, 8) a = __simd_splat(3.0)
    Simd(f64, 8) b = __simd_splat(4.0)
    Simd(f64, 8) c = a + b
    check(__simd_lane(c, 0) == 7.0, "f64 splat+add+lane")
    check(__simd_lane(c, 7) == 7.0, "f64 lane 7")
    check(__simd_reduce_add(c) == 56.0, "f64 reduce_add")      // 7 * 8

    Simd(f64, 8) z = __simd_zero()
    check(__simd_lane(z, 5) == 0.0, "f64 zero")

    Simd(f64, 8) f = __simd_fma(a, b, a)                       // 3*4 + 3 = 15
    check(__simd_lane(f, 0) == 15.0, "f64 fma")

    Simd(f64, 8) d = b - a                                     // 1.0
    check(__simd_lane(d, 0) == 1.0, "f64 sub")
    Simd(f64, 8) m = a * b                                     // 12.0
    check(__simd_lane(m, 3) == 12.0, "f64 mul")
    Simd(f64, 8) q = b / a                                     // 4/3 ~= 1.333
    check(__simd_lane(q, 0) > 1.33, "f64 div")

    // ---- f32 (primary AI element type; literal f64 coerced to f32) ----
    Simd(f32, 16) p = __simd_splat(2.5)
    Simd(f32, 16) pp = p * p                                   // 6.25
    f32 l0 = __simd_lane(pp, 0)
    check((l0 as f64) == 6.25, "f32 splat+mul+lane")
    f32 sm = __simd_reduce_add(pp)                             // 6.25 * 16 = 100
    check((sm as f64) == 100.0, "f32 reduce_add")
    @print(l0)                                                 // exercises print(f32) fix -> 6.250000

    // ---- i32 ----
    Simd(i32, 8) ia = __simd_splat(10)
    Simd(i32, 8) ib = __simd_splat(5)
    Simd(i32, 8) ic = ia + ib                                  // 15
    check(__simd_lane(ic, 0) == 15, "i32 add")
    check(__simd_reduce_add(ic) == 120, "i32 reduce_add")      // 15 * 8

    // ---- memory load/store (the micro-kernel's data path) ----
    // 32 f32 buffer = two 16-lane vectors.
    *f32 buf = sc.malloc(32 * sizeof(f32)) as *f32
    Simd(f32, 16) v0 = __simd_splat(1.5)
    Simd(f32, 16) v1 = __simd_splat(2.0)
    __simd_store(buf, 0, v0)                                    // lanes 0..15
    __simd_store(buf, 16, v1)                                   // lanes 16..31 (offset)
    Simd(f32, 16) r0 = __simd_load(buf, 0)
    Simd(f32, 16) r1 = __simd_load(buf, 16)
    check((__simd_reduce_add(r0) as f64) == 24.0, "store/load lo")   // 1.5 * 16
    check((__simd_reduce_add(r1) as f64) == 32.0, "store/load hi")   // 2.0 * 16
    Simd(f32, 16) lsum = r0 + r1                                // 3.5 each lane
    __simd_store(buf, 0, lsum)
    Simd(f32, 16) back = __simd_load(buf, 0)
    check((__simd_lane(back, 7) as f64) == 3.5, "store/load roundtrip")
    sc.free(buf as *u8)

    // ---- micro-kernel pattern: SAXPY  y[i] = a*x[i] + y[i], 16-wide ----
    // The same load -> fma -> store inner loop a GEMM micro-kernel runs, here
    // over a 1-D vector so the data path (load/fma/store across a loop) is the
    // thing under test. N divisible by 16 (fringe masking is Phase 3).
    int N = 64
    *f32 xs = sc.malloc(N * sizeof(f32)) as *f32
    *f32 ys = sc.malloc(N * sizeof(f32)) as *f32
    for i in 0..N {
        xs[i] = i as f32
        ys[i] = 1.0 as f32
    }
    Simd(f32, 16) va = __simd_splat(2.0)                        // a = 2.0
    int kk = 0
    while kk < N {
        Simd(f32, 16) vx = __simd_load(xs, kk)
        Simd(f32, 16) vy = __simd_load(ys, kk)
        vy = __simd_fma(va, vx, vy)                            // 2*x + y
        __simd_store(ys, kk, vy)
        kk = kk + 16
    }
    bool kok = true
    for i in 0..N {
        if (ys[i] as f64) != ((2 * i + 1) as f64) { kok = false }
    }
    check(kok, "saxpy micro-kernel")
    sc.free(xs as *u8)
    sc.free(ys as *u8)

    // ---- max / min (element-wise) + horizontal reduce_max/min ----
    // Activation primitives: ReLU = max(x, 0), SiLU clamp, softmax needs reduce_max.
    Simd(f32, 16) ma = __simd_splat(3.0)
    Simd(f32, 16) mb = __simd_splat(5.0)
    check((__simd_lane(__simd_max(ma, mb), 0) as f64) == 5.0, "f32 max")
    check((__simd_lane(__simd_min(ma, mb), 0) as f64) == 3.0, "f32 min")

    // reduce over distinct lanes (load -4 .. 11 from a buffer)
    *f32 mbuf = sc.malloc(16 * sizeof(f32)) as *f32
    for i in 0..16 { mbuf[i] = (i - 4) as f32 }
    Simd(f32, 16) mv = __simd_load(mbuf, 0)
    check((__simd_reduce_max(mv) as f64) == 11.0, "f32 reduce_max")
    check((__simd_reduce_min(mv) as f64) == (0.0 - 4.0), "f32 reduce_min")
    sc.free(mbuf as *u8)

    // i32 (signed) max/min + reduce
    Simd(i32, 8) za = __simd_splat(7)
    Simd(i32, 8) zb = __simd_splat(2)
    check(__simd_lane(__simd_max(za, zb), 0) == 7, "i32 max")
    check(__simd_lane(__simd_min(za, zb), 0) == 2, "i32 min")
    *i32 zbuf = sc.malloc(8 * sizeof(i32)) as *i32
    for i in 0..8 { zbuf[i] = (i * 3 - 5) as i32 }   // -5 .. 16
    Simd(i32, 8) zv = __simd_load(zbuf, 0)
    check(__simd_reduce_max(zv) == 16, "i32 reduce_max")
    check(__simd_reduce_min(zv) == (0 - 5), "i32 reduce_min")
    sc.free(zbuf as *u8)

    // ---- masked load/store (fringe: non-multiple-of-16 tails) ----
    *f32 src = sc.malloc(16 * sizeof(f32)) as *f32
    for i in 0..16 { src[i] = (i + 1) as f32 }         // 1 .. 16
    Simd(f32, 16) mload = __simd_load_masked(src, 0, 6) // lanes 0..5 = 1..6, rest 0
    check((__simd_reduce_add(mload) as f64) == 21.0, "masked load (1+..+6)")
    check((__simd_lane(mload, 5) as f64) == 6.0, "masked load lane 5")
    check((__simd_lane(mload, 6) as f64) == 0.0, "masked load lane 6 zeroed")

    *f32 dst = sc.malloc(16 * sizeof(f32)) as *f32
    for i in 0..16 { dst[i] = (0.0 - 1.0) as f32 }     // sentinel -1
    __simd_store_masked(dst, 0, mload, 6)              // write only lanes 0..5
    check((dst[0] as f64) == 1.0, "masked store first")
    check((dst[5] as f64) == 6.0, "masked store last written")
    check((dst[6] as f64) == (0.0 - 1.0), "masked store untouched 6")
    check((dst[15] as f64) == (0.0 - 1.0), "masked store untouched 15")
    sc.free(src as *u8)
    sc.free(dst as *u8)

    @print("SIMD OK")
}
