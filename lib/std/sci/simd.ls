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
//   exp(v)      elementwise exp,  Cephes poly + 2^n (range reduction)
//   atan(v)     elementwise atan, branchless full-range minimax
//
// exp uses the __simd_floor + __simd_bitcast primitives (added for it); atan is
// pure min/max/div/fma (no select needed). Both serve Transformer softmax /
// LayerNorm and the PHY timing head. rsqrt is still deferred (needs __simd_sqrt).

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

// Elementwise exp (Cephes single-precision, rel err ~1e-7). Range-reduce
// x = n*ln2 + r, |r|<=ln2/2, exp(r) by a degree-6 poly, then *2^n built from the
// integer exponent field. Needs __simd_floor + integer convert/mul + __simd_bitcast
// (the primitives the old "deferred" note called for). For softmax / LayerNorm.
def exp(Simd(f32, 16) x) -> Simd(f32, 16) {
    Simd(f32, 16) hi = __simd_splat(88.3762626647949)
    Simd(f32, 16) lo = __simd_splat(-88.3762626647949)
    Simd(f32, 16) xc = __simd_max(__simd_min(x, hi), lo)
    Simd(f32, 16) LOG2EF = __simd_splat(1.44269504088896341)
    Simd(f32, 16) half   = __simd_splat(0.5)
    Simd(f32, 16) fx = __simd_floor(__simd_fma(xc, LOG2EF, half))   // n = floor(x*log2e + 0.5)
    // r = x - n*ln2 (two-part ln2 for accuracy)
    xc = xc - fx * __simd_splat(0.693359375)
    xc = xc - fx * __simd_splat(-2.12194440e-4)
    Simd(f32, 16) x2 = xc * xc
    Simd(f32, 16) p = __simd_splat(1.9875691500e-4)
    p = __simd_fma(p, xc, __simd_splat(1.3981999507e-3))
    p = __simd_fma(p, xc, __simd_splat(8.3334519073e-3))
    p = __simd_fma(p, xc, __simd_splat(4.1665795894e-2))
    p = __simd_fma(p, xc, __simd_splat(1.6666665459e-1))
    p = __simd_fma(p, xc, __simd_splat(5.0000001201e-1))
    p = __simd_fma(p, x2, xc)            // p = p*r^2 + r
    p = p + __simd_splat(1.0)
    // 2^n : emm = (int(n)+127) * 2^23, reinterpreted as f32 (clamp keeps n in [-127,127]).
    Simd(i32, 16) emm   = __simd_cast(fx)         // f32 -> i32 (n is integer-valued)
    Simd(i32, 16) c127  = __simd_splat(127)
    Simd(i32, 16) c2p23 = __simd_splat(8388608)   // 2^23  (int * = exponent shift, no <<)
    emm = (emm + c127) * c2p23
    Simd(f32, 16) pow2 = __simd_bitcast(emm)
    return p * pow2
}

// Elementwise atan (abs err ~1e-4). Branchless full-range: reduce the argument to
// z = min(|x|,1)/max(|x|,1) in [0,1] (= |x| if |x|<=1 else 1/|x|), degree-9 odd
// minimax poly, fold the |x|>1 correction (pi/2 - p) with a clamp-step, re-sign.
// No select/compare intrinsic needed — only min/max/mul/div/fma.
def atan(Simd(f32, 16) x) -> Simd(f32, 16) {
    Simd(f32, 16) zero = __simd_zero()
    Simd(f32, 16) one  = __simd_splat(1.0)
    Simd(f32, 16) ax = __simd_max(x, zero - x)               // |x|
    Simd(f32, 16) z  = __simd_min(ax, one) / __simd_max(ax, one)
    Simd(f32, 16) z2 = z * z
    Simd(f32, 16) p = __simd_splat(0.0208351)
    p = __simd_fma(p, z2, __simd_splat(-0.0851330))
    p = __simd_fma(p, z2, __simd_splat(0.1801410))
    p = __simd_fma(p, z2, __simd_splat(-0.3302995))
    p = __simd_fma(p, z2, __simd_splat(0.9998660))
    p = p * z                                                // atan(z), z in [0,1]
    // step s = 1 if |x|>1 else 0  (clamp((|x|-1)*BIG, 0, 1))
    Simd(f32, 16) s = __simd_min(__simd_max((ax - one) * __simd_splat(1.0e9), zero), one)
    Simd(f32, 16) halfpi = __simd_splat(1.57079637)
    Simd(f32, 16) r = p + s * (halfpi - p - p)               // |x|<=1: p ; |x|>1: pi/2 - p
    Simd(f32, 16) sign = x / (ax + __simd_splat(1.0e-30))    // +-1 (0 at x=0)
    return sign * r
}
