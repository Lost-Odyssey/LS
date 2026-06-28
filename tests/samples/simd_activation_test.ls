// std.sci.simd — vectorized activations (tanh / sigmoid / silu / relu) on
// Simd(f32, 16), checked per-lane against a scalar f64 reference computed with
// math.exp. 16 distinct lane values (-8 .. 7) exercise real per-lane data, not
// just a splat. Simd is POD so --memcheck is 0/0/0 (the buffers are freed).

import std.sci.simd as act
import std.core.math as math
import std.sys.c as sc

def approx(f64 a, f64 b, f64 tol) -> bool {
    f64 d = a - b
    if d < 0.0 { d = 0.0 - d }
    return d < tol
}

def main() {
    *f32 xb = sc.malloc(16 * sizeof(f32)) as *f32
    for i in 0..16 { xb[i] = (i - 8) as f32 }          // -8 .. 7
    Simd(f32, 16) x = __simd_load(xb, 0)

    Simd(f32, 16) vt = act.tanh(x)
    Simd(f32, 16) vs = act.sigmoid(x)
    Simd(f32, 16) vu = act.silu(x)
    Simd(f32, 16) vr = act.relu(x)

    *f32 tb = sc.malloc(16 * sizeof(f32)) as *f32
    *f32 sb = sc.malloc(16 * sizeof(f32)) as *f32
    *f32 ub = sc.malloc(16 * sizeof(f32)) as *f32
    *f32 rb = sc.malloc(16 * sizeof(f32)) as *f32
    __simd_store(tb, 0, vt)
    __simd_store(sb, 0, vs)
    __simd_store(ub, 0, vu)
    __simd_store(rb, 0, vr)

    bool ok = true
    for i in 0..16 {
        f64 xv = xb[i] as f64
        f64 e1 = math.exp(xv)
        f64 e2 = math.exp(0.0 - xv)
        f64 th = (e1 - e2) / (e1 + e2)                 // tanh ref
        f64 sg = 1.0 / (1.0 + e2)                      // sigmoid ref = 1/(1+e^-x)
        f64 si = xv * sg                               // silu ref
        f64 rl = xv
        if rl < 0.0 { rl = 0.0 }                       // relu ref

        if !approx(tb[i] as f64, th, 0.002) {
            ok = false
            @print(f"ACT FAIL: tanh lane {i} got={tb[i] as f64} want={th}")
        }
        if !approx(sb[i] as f64, sg, 0.002) {
            ok = false
            @print(f"ACT FAIL: sigmoid lane {i} got={sb[i] as f64} want={sg}")
        }
        if !approx(ub[i] as f64, si, 0.002) {
            ok = false
            @print(f"ACT FAIL: silu lane {i} got={ub[i] as f64} want={si}")
        }
        if !approx(rb[i] as f64, rl, 0.0001) {
            ok = false
            @print(f"ACT FAIL: relu lane {i} got={rb[i] as f64} want={rl}")
        }
    }

    sc.free(xb as *u8)
    sc.free(tb as *u8)
    sc.free(sb as *u8)
    sc.free(ub as *u8)
    sc.free(rb as *u8)

    if ok { @print("ACT OK") }
}
