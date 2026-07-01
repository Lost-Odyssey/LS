// std.sci.simd — vectorized exp (vs math.exp) and atan (vs a known table) on
// Simd(f32, 16). Exercises the new __simd_floor / __simd_bitcast primitives.
import std.sci.simd as smd
import std.core.math as math
import std.sys.c as sc

def approx(f64 a, f64 b, f64 tol) -> bool {
    f64 d = a - b
    if d < 0.0 { d = 0.0 - d }
    return d < tol
}

def main() {
    bool ok = true

    // ---- exp vs math.exp (per lane, relative tol) ----
    *f32 xb = sc.malloc(16 * sizeof(f32)) as *f32
    for i in 0..16 { xb[i] = (((i as f32) - 8.0) * 0.5) as f32 }      // -4.0 .. 3.5
    Simd(f32, 16) x = __simd_load(xb, 0)
    Simd(f32, 16) ve = smd.exp(x)
    *f32 eb = sc.malloc(16 * sizeof(f32)) as *f32
    __simd_store(eb, 0, ve)
    for i in 0..16 {
        f64 r = math.exp(xb[i] as f64)
        f64 g = eb[i] as f64
        if !approx(g, r, r * 0.002 + 0.0001) { ok = false; @print(f"exp[{i}] got={g} ref={r}\n") }
    }

    // ---- atan vs known reference table (8 points) ----
    *f32 ib = sc.malloc(16 * sizeof(f32)) as *f32
    *f64 rb = sc.malloc(16 * sizeof(f64)) as *f64
    ib[0] = 0.0 as f32;          rb[0] = 0.0
    ib[1] = 1.0 as f32;          rb[1] = 0.7853982
    ib[2] = (0.0 - 1.0) as f32;  rb[2] = 0.0 - 0.7853982
    ib[3] = 0.5 as f32;          rb[3] = 0.4636476
    ib[4] = 2.0 as f32;          rb[4] = 1.1071487
    ib[5] = (0.0 - 2.0) as f32;  rb[5] = 0.0 - 1.1071487
    ib[6] = 3.0 as f32;          rb[6] = 1.2490458
    ib[7] = (0.0 - 5.0) as f32;  rb[7] = 0.0 - 1.3734008
    for i in 8..16 { ib[i] = 0.0 as f32; rb[i] = 0.0 }
    Simd(f32, 16) xa = __simd_load(ib, 0)
    Simd(f32, 16) va = smd.atan(xa)
    *f32 ab = sc.malloc(16 * sizeof(f32)) as *f32
    __simd_store(ab, 0, va)
    for i in 0..16 {
        f64 g = ab[i] as f64
        if !approx(g, rb[i], 0.002) { ok = false; @print(f"atan[{i}] got={g} ref={rb[i]}\n") }
    }

    sc.free(xb as *u8); sc.free(eb as *u8); sc.free(ib as *u8); sc.free(rb as *u8); sc.free(ab as *u8)
    if ok { @print("EXPATAN OK\n") } else { @print("EXPATAN FAIL\n") }
}
