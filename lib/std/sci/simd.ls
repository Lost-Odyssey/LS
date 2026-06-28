// std/sci/simd.ls — vectorized activation / transcendental functions on Simd.
//
// N is a compile-time constant and LS has no const generics, so these cannot be
// written generically over the lane count (see docs/plan_simd.md §4.3); they are
// fixed to Simd(f32, 16) — the Granite Rapids AVX-512 natural f32 width (on AVX2
// the backend splits every op into 2x ymm, still correct). Everything is built
// from portable __simd_* primitives + operators, never @llvm.exp / libm: a libm
// call would have no JIT symbol and is not vectorizable. For the AI physical-layer
// inference path: SiLU/Swish activation, sigmoid, tanh, ReLU.
//
//   tanh(v)     elementwise tanh,  rational minimax, accurate over the full range
//   sigmoid(v)  1/(1+e^-x) = 0.5 + 0.5*tanh(0.5x)
//   silu(v)     x * sigmoid(x)   (a.k.a. Swish; the target CNN's activation)
//   relu(v)     max(x, 0)
//
// exp / rsqrt are deferred: a polynomial exp needs integer shift + bitcast +
// floor primitives (range reduction / 2^n scaling), rsqrt needs __simd_sqrt.
// Both serve Transformer softmax / LayerNorm, which are Tier 2 (plan §12).

// Elementwise tanh via a degree-13 / degree-6 rational minimax (the Eigen
// `ptanh` coefficients): tanh(x) ~= P(x)/Q(x) after clamping |x| to the range
// where tanh has already saturated to +-1 in f32. Only mul/add (fma) + clamp.
def tanh(Simd(f32, 16) x) -> Simd(f32, 16) {
    Simd(f32, 16) hi = __simd_splat(7.99881172180175781)
    Simd(f32, 16) lo = __simd_splat(-7.99881172180175781)
    Simd(f32, 16) xc = __simd_max(__simd_min(x, hi), lo)
    Simd(f32, 16) x2 = xc * xc

    // numerator P(x) = x * (a1 + x2*(a3 + x2*(a5 + ... + x2*a13)))   (Horner)
    Simd(f32, 16) a13 = __simd_splat(-2.76076847742355e-16)
    Simd(f32, 16) a11 = __simd_splat(2.00018790482477e-13)
    Simd(f32, 16) a9  = __simd_splat(-8.60467152213735e-11)
    Simd(f32, 16) a7  = __simd_splat(5.12229709037114e-08)
    Simd(f32, 16) a5  = __simd_splat(1.48572235717979e-05)
    Simd(f32, 16) a3  = __simd_splat(6.37261928875436e-04)
    Simd(f32, 16) a1  = __simd_splat(4.89352455891786e-03)
    Simd(f32, 16) p = a13
    p = __simd_fma(x2, p, a11)
    p = __simd_fma(x2, p, a9)
    p = __simd_fma(x2, p, a7)
    p = __simd_fma(x2, p, a5)
    p = __simd_fma(x2, p, a3)
    p = __simd_fma(x2, p, a1)
    p = p * xc

    // denominator Q(x) = b0 + x2*(b2 + x2*(b4 + x2*b6))   (Horner)
    Simd(f32, 16) b6 = __simd_splat(1.19825839466702e-06)
    Simd(f32, 16) b4 = __simd_splat(1.18534705686654e-04)
    Simd(f32, 16) b2 = __simd_splat(2.26843463243900e-03)
    Simd(f32, 16) b0 = __simd_splat(4.89352518554385e-03)
    Simd(f32, 16) q = b6
    q = __simd_fma(x2, q, b4)
    q = __simd_fma(x2, q, b2)
    q = __simd_fma(x2, q, b0)

    return p / q
}

// Logistic sigmoid, derived from tanh: sigmoid(x) = 0.5 + 0.5*tanh(x/2).
// Reuses tanh's saturation/clamp so it is well-behaved over the full range.
def sigmoid(Simd(f32, 16) x) -> Simd(f32, 16) {
    Simd(f32, 16) half = __simd_splat(0.5)
    Simd(f32, 16) t = tanh(half * x)
    return half + half * t
}

// SiLU / Swish: x * sigmoid(x). A common CNN activation.
// Inference fast path: sigmoid = 0.5 + 0.5*tanh(0.5x) with a *reduced* degree-7/4
// rational tanh (drops the degree-9/11/13 numerator + degree-6 denominator terms of
// `tanh()` above — their coefficients are ~1e-11..1e-16 and only matter as |arg|->8,
// where tanh is already saturated). Cheaper than calling tanh() (fewer FMA, same one
// divide). Validated to <1e-3 vs exact over [-10,10] (silu_ok) and within golden tol.
def silu(Simd(f32, 16) x) -> Simd(f32, 16) {
    Simd(f32, 16) half = __simd_splat(0.5)
    Simd(f32, 16) y = half * x
    Simd(f32, 16) hi = __simd_splat(7.99881172180175781)
    Simd(f32, 16) lo = __simd_splat(-7.99881172180175781)
    Simd(f32, 16) yc = __simd_max(__simd_min(y, hi), lo)
    Simd(f32, 16) y2 = yc * yc
    // tanh(y) ~= P/Q, degree-7/4 minimax least-squares fit over [-8,8] (maxerr 6e-4).
    // numerator P(y) = y * (a1 + y2*(a3 + y2*(a5 + y2*a7)))
    Simd(f32, 16) p = __simd_splat(-1.160025105626e-06)
    p = __simd_fma(y2, p, __simd_splat(7.028989453653e-04))
    p = __simd_fma(y2, p, __simd_splat(9.935757896942e-02))
    p = __simd_fma(y2, p, __simd_splat(9.980187882896e-01))
    p = p * yc
    // denominator Q(y) = 1 + y2*(b2 + y2*b4)
    Simd(f32, 16) q = __simd_splat(1.244133230638e-02)
    q = __simd_fma(y2, q, __simd_splat(4.296432622344e-01))
    q = __simd_fma(y2, q, __simd_splat(1.0))
    Simd(f32, 16) t = p / q
    Simd(f32, 16) sig = half + half * t
    return x * sig
}

// Rectified linear unit: max(x, 0).
def relu(Simd(f32, 16) x) -> Simd(f32, 16) {
    Simd(f32, 16) z = __simd_zero()
    return __simd_max(x, z)
}
