// emit-c coverage kernel: exercises the C-emitter's subset (scalars, pointers,
// Simd ops, control flow). Not run as LS — fed to `ls emit-c` and the generated
// C is compiled with clang. (It is also valid LS and type-checks.)

def axpy16(*f32 x, *f32 y, f32 a, int n) {
    Simd(f32, 16) va = __simd_splat(a)
    int k = 0
    while k + 16 <= n {
        Simd(f32, 16) vx = __simd_load(x, k)
        Simd(f32, 16) vy = __simd_load(y, k)
        vy = __simd_fma(va, vx, vy)
        __simd_store(y, k, vy)
        k = k + 16
    }
    // masked tail
    int rem = n - k
    if rem > 0 {
        Simd(f32, 16) vx = __simd_load_masked(x, k, rem)
        Simd(f32, 16) vy = __simd_load_masked(y, k, rem)
        Simd(f32, 16) vr = __simd_fma(va, vx, vy)
        __simd_store_masked(y, k, vr, rem)
    }
}

def relu_sum16(*f32 p) -> f32 {
    Simd(f32, 16) v = __simd_load(p, 0)
    Simd(f32, 16) z = __simd_zero()
    Simd(f32, 16) r = __simd_max(v, z)
    Simd(f32, 16) m = __simd_min(v, z)
    f32 hi = __simd_reduce_max(r)
    f32 lo = __simd_reduce_min(m)
    f32 s = __simd_reduce_add(r)
    return (s + hi) - lo
}

def elementwise16(*f32 a, *f32 b, *f32 c) {
    Simd(f32, 16) x = __simd_load(a, 0)
    Simd(f32, 16) y = __simd_load(b, 0)
    Simd(f32, 16) r = (x + y) * x - y / x
    __simd_store(c, 0, r)
    f32 first = __simd_lane(r, 0)
    c[0] = first
}

def scalar_helper(int m, int kk) -> int {
    int acc = 0
    for i in 0..m {
        acc = acc + (i * 2)
    }
    for (int j = 0; j < kk; j = j + 1) {
        acc = acc + j
    }
    return acc
}

def uses_helper(int n) -> int {
    return scalar_helper(n, n) + (sizeof(f32) as int)
}
