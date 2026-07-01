// std/sci/nn.ls — FP32 neural-net kernels built on Simd. The core is a blocked
// single-precision GEMM (sgemm): C = A·B, the operation every dense / conv /
// attention layer reduces to. The register-resident micro-kernel IS LS — no
// C/FFI — which was the whole point of the Simd primitive (plan_simd.md §1).
//
// Layout: row-major. A is M×K (lda=K), B is K×N (ldb=N), C is M×N (ldc=N).
// sgemm OVERWRITES C (C = A·B, not +=). Everything is f32 throughout.
//
// The micro-kernel holds an mr×16 block of C in vector registers (one Simd per
// row) for the full K reduction, rank-1 updating it: each k-step broadcasts mr
// A-scalars and FMAs them against one contiguous 16-wide B row. Arithmetic
// intensity = mr FMAs per B-load. The 6×16 kernel (6 accumulators) drives the
// M-divisible-by-6 bulk; a 1×16 kernel mops up the M tail; a scalar block
// handles the N tail (columns past the last multiple of 16). N divisible by 16
// (e.g. the target 256) takes the all-vector path with no scalar tail.
//
// Two GEMM drivers: `sgemm` (simple, reads B by stride — good when A·B fits
// cache) and `sgemm_packed` (BLIS GEBP: A/B packing + cache blocking mc/kc/nc
// derived from the live cache hierarchy — wins on large matrices that blow L2).
// Both share the uk_6x16 register tile. The wider GNR-tuned 12×32 kernel (24 zmm
// accumulators) is for AVX-512 hosts; on AVX2 the 6×16 tile is the right size.

import std.sys.c as c
import std.core.math as math
import std.sci.simd as smd
import std.sync.thread as thread

// 12×32 micro-kernel (C2, the BLIS-family AVX-512 server SGEMM register tile):
// 12 rows × 32 cols = 24 zmm accumulators. Per k-step: two 16-wide B-loads (the
// 32-col panel) + 12 A-broadcasts, each broadcast feeding 2 FMAs → 24 FMAs from
// 2 loads + 12 broadcasts. This is the canonical f32 GNR kernel (≈24 of 32 zmm as
// accumulators amortizes both B-load and A-broadcast). Requires N>=32 at bj; the
// sgemm driver falls through to 12×16 / 6×16 / 1×16 for narrower remainders.
// (On real GNR's two 512-bit FMA ports this hides the FMA-recurrence; on the
// 1-FMA-port llvm-mca model it is throughput-equivalent. Correctness-first.)
def uk_12x32(*f32 A, *f32 B, *f32 C, int K, int lda, int ldb, int ldc,
             int ai, int bj) {
    Simd(f32, 16) p0 = __simd_zero()
    Simd(f32, 16) p1 = __simd_zero()
    Simd(f32, 16) p2 = __simd_zero()
    Simd(f32, 16) p3 = __simd_zero()
    Simd(f32, 16) p4 = __simd_zero()
    Simd(f32, 16) p5 = __simd_zero()
    Simd(f32, 16) p6 = __simd_zero()
    Simd(f32, 16) p7 = __simd_zero()
    Simd(f32, 16) p8 = __simd_zero()
    Simd(f32, 16) p9 = __simd_zero()
    Simd(f32, 16) p10 = __simd_zero()
    Simd(f32, 16) p11 = __simd_zero()
    Simd(f32, 16) q0 = __simd_zero()
    Simd(f32, 16) q1 = __simd_zero()
    Simd(f32, 16) q2 = __simd_zero()
    Simd(f32, 16) q3 = __simd_zero()
    Simd(f32, 16) q4 = __simd_zero()
    Simd(f32, 16) q5 = __simd_zero()
    Simd(f32, 16) q6 = __simd_zero()
    Simd(f32, 16) q7 = __simd_zero()
    Simd(f32, 16) q8 = __simd_zero()
    Simd(f32, 16) q9 = __simd_zero()
    Simd(f32, 16) q10 = __simd_zero()
    Simd(f32, 16) q11 = __simd_zero()
    int k = 0
    while k < K {
        Simd(f32, 16) b0 = __simd_load(B, k * ldb + bj)
        Simd(f32, 16) b1 = __simd_load(B, k * ldb + bj + 16)
        Simd(f32, 16) a0 = __simd_splat(A[(ai + 0) * lda + k])
        p0 = __simd_fma(a0, b0, p0)
        q0 = __simd_fma(a0, b1, q0)
        Simd(f32, 16) a1 = __simd_splat(A[(ai + 1) * lda + k])
        p1 = __simd_fma(a1, b0, p1)
        q1 = __simd_fma(a1, b1, q1)
        Simd(f32, 16) a2 = __simd_splat(A[(ai + 2) * lda + k])
        p2 = __simd_fma(a2, b0, p2)
        q2 = __simd_fma(a2, b1, q2)
        Simd(f32, 16) a3 = __simd_splat(A[(ai + 3) * lda + k])
        p3 = __simd_fma(a3, b0, p3)
        q3 = __simd_fma(a3, b1, q3)
        Simd(f32, 16) a4 = __simd_splat(A[(ai + 4) * lda + k])
        p4 = __simd_fma(a4, b0, p4)
        q4 = __simd_fma(a4, b1, q4)
        Simd(f32, 16) a5 = __simd_splat(A[(ai + 5) * lda + k])
        p5 = __simd_fma(a5, b0, p5)
        q5 = __simd_fma(a5, b1, q5)
        Simd(f32, 16) a6 = __simd_splat(A[(ai + 6) * lda + k])
        p6 = __simd_fma(a6, b0, p6)
        q6 = __simd_fma(a6, b1, q6)
        Simd(f32, 16) a7 = __simd_splat(A[(ai + 7) * lda + k])
        p7 = __simd_fma(a7, b0, p7)
        q7 = __simd_fma(a7, b1, q7)
        Simd(f32, 16) a8 = __simd_splat(A[(ai + 8) * lda + k])
        p8 = __simd_fma(a8, b0, p8)
        q8 = __simd_fma(a8, b1, q8)
        Simd(f32, 16) a9 = __simd_splat(A[(ai + 9) * lda + k])
        p9 = __simd_fma(a9, b0, p9)
        q9 = __simd_fma(a9, b1, q9)
        Simd(f32, 16) a10 = __simd_splat(A[(ai + 10) * lda + k])
        p10 = __simd_fma(a10, b0, p10)
        q10 = __simd_fma(a10, b1, q10)
        Simd(f32, 16) a11 = __simd_splat(A[(ai + 11) * lda + k])
        p11 = __simd_fma(a11, b0, p11)
        q11 = __simd_fma(a11, b1, q11)
        k = k + 1
    }
    __simd_store(C, (ai + 0) * ldc + bj, p0)
    __simd_store(C, (ai + 0) * ldc + bj + 16, q0)
    __simd_store(C, (ai + 1) * ldc + bj, p1)
    __simd_store(C, (ai + 1) * ldc + bj + 16, q1)
    __simd_store(C, (ai + 2) * ldc + bj, p2)
    __simd_store(C, (ai + 2) * ldc + bj + 16, q2)
    __simd_store(C, (ai + 3) * ldc + bj, p3)
    __simd_store(C, (ai + 3) * ldc + bj + 16, q3)
    __simd_store(C, (ai + 4) * ldc + bj, p4)
    __simd_store(C, (ai + 4) * ldc + bj + 16, q4)
    __simd_store(C, (ai + 5) * ldc + bj, p5)
    __simd_store(C, (ai + 5) * ldc + bj + 16, q5)
    __simd_store(C, (ai + 6) * ldc + bj, p6)
    __simd_store(C, (ai + 6) * ldc + bj + 16, q6)
    __simd_store(C, (ai + 7) * ldc + bj, p7)
    __simd_store(C, (ai + 7) * ldc + bj + 16, q7)
    __simd_store(C, (ai + 8) * ldc + bj, p8)
    __simd_store(C, (ai + 8) * ldc + bj + 16, q8)
    __simd_store(C, (ai + 9) * ldc + bj, p9)
    __simd_store(C, (ai + 9) * ldc + bj + 16, q9)
    __simd_store(C, (ai + 10) * ldc + bj, p10)
    __simd_store(C, (ai + 10) * ldc + bj + 16, q10)
    __simd_store(C, (ai + 11) * ldc + bj, p11)
    __simd_store(C, (ai + 11) * ldc + bj + 16, q11)
}

// 6×16 micro-kernel: C[ai..ai+6][bj..bj+16] = A[ai..][0..K] · B[0..K][bj..bj+16].
// Six accumulators stay in registers across the whole k loop (→ 6 zmm on GNR).
def uk_6x16(*f32 A, *f32 B, *f32 C, int K, int lda, int ldb, int ldc,
            int ai, int bj) {
    Simd(f32, 16) c0 = __simd_zero()
    Simd(f32, 16) c1 = __simd_zero()
    Simd(f32, 16) c2 = __simd_zero()
    Simd(f32, 16) c3 = __simd_zero()
    Simd(f32, 16) c4 = __simd_zero()
    Simd(f32, 16) c5 = __simd_zero()
    int k = 0
    while k < K {
        Simd(f32, 16) b = __simd_load(B, k * ldb + bj)
        Simd(f32, 16) a0 = __simd_splat(A[(ai + 0) * lda + k])
        Simd(f32, 16) a1 = __simd_splat(A[(ai + 1) * lda + k])
        Simd(f32, 16) a2 = __simd_splat(A[(ai + 2) * lda + k])
        Simd(f32, 16) a3 = __simd_splat(A[(ai + 3) * lda + k])
        Simd(f32, 16) a4 = __simd_splat(A[(ai + 4) * lda + k])
        Simd(f32, 16) a5 = __simd_splat(A[(ai + 5) * lda + k])
        c0 = __simd_fma(a0, b, c0)
        c1 = __simd_fma(a1, b, c1)
        c2 = __simd_fma(a2, b, c2)
        c3 = __simd_fma(a3, b, c3)
        c4 = __simd_fma(a4, b, c4)
        c5 = __simd_fma(a5, b, c5)
        k = k + 1
    }
    __simd_store(C, (ai + 0) * ldc + bj, c0)
    __simd_store(C, (ai + 1) * ldc + bj, c1)
    __simd_store(C, (ai + 2) * ldc + bj, c2)
    __simd_store(C, (ai + 3) * ldc + bj, c3)
    __simd_store(C, (ai + 4) * ldc + bj, c4)
    __simd_store(C, (ai + 5) * ldc + bj, c5)
}

// 12×16 micro-kernel (C2): 12 C-rows held in registers across the K reduction.
// One B-load feeds 12 FMAs (vs 6 for uk_6x16) — fewer B-loads/broadcasts per FMA
// (marginally fewer total uops) and, if the target runs two 512-bit FMA ports
// (real GNR), 12 accumulators ≥ 2×latency hide the FMA recurrence that 6 cannot
// (6 < 2×4). On a 1-FMA-port model (what llvm-mca's graniterapids reports) it is
// throughput-equivalent — never worse, possibly better. 12 acc + B + a-splat =
// ~14 zmm live, well within 32.
def uk_12x16(*f32 A, *f32 B, *f32 C, int K, int lda, int ldb, int ldc,
             int ai, int bj) {
    Simd(f32, 16) c0 = __simd_zero()
    Simd(f32, 16) c1 = __simd_zero()
    Simd(f32, 16) c2 = __simd_zero()
    Simd(f32, 16) c3 = __simd_zero()
    Simd(f32, 16) c4 = __simd_zero()
    Simd(f32, 16) c5 = __simd_zero()
    Simd(f32, 16) c6 = __simd_zero()
    Simd(f32, 16) c7 = __simd_zero()
    Simd(f32, 16) c8 = __simd_zero()
    Simd(f32, 16) c9 = __simd_zero()
    Simd(f32, 16) c10 = __simd_zero()
    Simd(f32, 16) c11 = __simd_zero()
    int k = 0
    while k < K {
        Simd(f32, 16) b = __simd_load(B, k * ldb + bj)
        c0 = __simd_fma(__simd_splat(A[(ai + 0) * lda + k]), b, c0)
        c1 = __simd_fma(__simd_splat(A[(ai + 1) * lda + k]), b, c1)
        c2 = __simd_fma(__simd_splat(A[(ai + 2) * lda + k]), b, c2)
        c3 = __simd_fma(__simd_splat(A[(ai + 3) * lda + k]), b, c3)
        c4 = __simd_fma(__simd_splat(A[(ai + 4) * lda + k]), b, c4)
        c5 = __simd_fma(__simd_splat(A[(ai + 5) * lda + k]), b, c5)
        c6 = __simd_fma(__simd_splat(A[(ai + 6) * lda + k]), b, c6)
        c7 = __simd_fma(__simd_splat(A[(ai + 7) * lda + k]), b, c7)
        c8 = __simd_fma(__simd_splat(A[(ai + 8) * lda + k]), b, c8)
        c9 = __simd_fma(__simd_splat(A[(ai + 9) * lda + k]), b, c9)
        c10 = __simd_fma(__simd_splat(A[(ai + 10) * lda + k]), b, c10)
        c11 = __simd_fma(__simd_splat(A[(ai + 11) * lda + k]), b, c11)
        k = k + 1
    }
    __simd_store(C, (ai + 0) * ldc + bj, c0)
    __simd_store(C, (ai + 1) * ldc + bj, c1)
    __simd_store(C, (ai + 2) * ldc + bj, c2)
    __simd_store(C, (ai + 3) * ldc + bj, c3)
    __simd_store(C, (ai + 4) * ldc + bj, c4)
    __simd_store(C, (ai + 5) * ldc + bj, c5)
    __simd_store(C, (ai + 6) * ldc + bj, c6)
    __simd_store(C, (ai + 7) * ldc + bj, c7)
    __simd_store(C, (ai + 8) * ldc + bj, c8)
    __simd_store(C, (ai + 9) * ldc + bj, c9)
    __simd_store(C, (ai + 10) * ldc + bj, c10)
    __simd_store(C, (ai + 11) * ldc + bj, c11)
}

// 1×16 micro-kernel for the M tail (rows not covered by a full 6-block).
def uk_1x16(*f32 A, *f32 B, *f32 C, int K, int lda, int ldb, int ldc,
            int ai, int bj) {
    Simd(f32, 16) c0 = __simd_zero()
    int k = 0
    while k < K {
        Simd(f32, 16) b = __simd_load(B, k * ldb + bj)
        Simd(f32, 16) a0 = __simd_splat(A[ai * lda + k])
        c0 = __simd_fma(a0, b, c0)
        k = k + 1
    }
    __simd_store(C, ai * ldc + bj, c0)
}

// Scalar fallback for the N tail: columns [jstart, N) over rows [i0, i0+rows).
def sgemm_tail(*f32 A, *f32 B, *f32 C, int K, int N, int i0, int rows, int jstart) {
    int r = 0
    while r < rows {
        int j = jstart
        while j < N {
            f32 s = 0.0 as f32
            int k = 0
            while k < K {
                s = s + A[(i0 + r) * K + k] * B[k * N + j]
                k = k + 1
            }
            C[(i0 + r) * N + j] = s
            j = j + 1
        }
        r = r + 1
    }
}

// AVX-512 driver: 12×32 (24 zmm acc) bulk → 12×16 → 6×16 → 1×16. Used by `sgemm`
// when the host has AVX-512; on AVX2 the 12×32 tile needs 48 ymm and spills, so
// `sgemm` routes AVX2 hosts to sgemm_avx2 instead.
def sgemm_wide(*f32 A, *f32 B, *f32 C, int M, int N, int K) {
    int nfull = (N / 16) * 16
    int n2 = (N / 32) * 32
    int i = 0
    // 12-row blocks: 12×32 (24 acc, BLIS-family tile) over the 32-wide column
    // panels, then 12×16 for a leftover 16-tile, then scalar N-tail. Falls through
    // to 6-row then 1-row for the M remainder.
    while i + 12 <= M {
        int j = 0
        while j < n2 {
            uk_12x32(A, B, C, K, K, N, N, i, j)
            j = j + 32
        }
        while j < nfull {
            uk_12x16(A, B, C, K, K, N, N, i, j)
            j = j + 16
        }
        if nfull < N { sgemm_tail(A, B, C, K, N, i, 12, nfull) }
        i = i + 12
    }
    // 6-row blocks: the high-intensity 6×16 kernel over all 16-wide column tiles.
    while i + 6 <= M {
        int j = 0
        while j < nfull {
            uk_6x16(A, B, C, K, K, N, N, i, j)
            j = j + 16
        }
        if nfull < N { sgemm_tail(A, B, C, K, N, i, 6, nfull) }
        i = i + 6
    }
    // M tail: leftover rows, one at a time.
    while i < M {
        int j = 0
        while j < nfull {
            uk_1x16(A, B, C, K, K, N, N, i, j)
            j = j + 16
        }
        if nfull < N { sgemm_tail(A, B, C, K, N, i, 1, nfull) }
        i = i + 1
    }
}

// AVX2 driver: 6×16 (12 ymm acc, fits AVX2's 16 ymm) bulk → 1×16 → scalar N-tail.
// 6×16 is the measured-best AVX2 f32 tile (vs 4×16 / 8×8): FMA-bound with 12
// independent accumulator chains to hide the FMA latency. The 12×32 wide tile
// spills here, so it loses to this on AVX2 (medium and tiny alike).
def sgemm_avx2(*f32 A, *f32 B, *f32 C, int M, int N, int K) {
    int nfull = (N / 16) * 16
    int i = 0
    while i + 6 <= M {
        int j = 0
        while j < nfull {
            uk_6x16(A, B, C, K, K, N, N, i, j)
            j = j + 16
        }
        if nfull < N { sgemm_tail(A, B, C, K, N, i, 6, nfull) }
        i = i + 6
    }
    while i < M {
        int j = 0
        while j < nfull {
            uk_1x16(A, B, C, K, K, N, N, i, j)
            j = j + 16
        }
        if nfull < N { sgemm_tail(A, B, C, K, N, i, 1, nfull) }
        i = i + 1
    }
}

// Host ISA cache for sgemm dispatch: -1 unknown, 0 AVX2, 1 AVX-512.
int g_sgemm_isa = -1

// C = A·B for row-major A(M×K), B(K×N), C(M×N). C is overwritten. Picks the 12×32
// (AVX-512) or 6×16 (AVX2) micro-kernel based on the host ISA — detected once via
// c.__ls_cpu_has_avx512() and cached. So one source runs near-optimally on both.
def sgemm(*f32 A, *f32 B, *f32 C, int M, int N, int K) {
    if g_sgemm_isa < 0 {
        if c.__ls_cpu_has_avx512() != 0 { g_sgemm_isa = 1 }
        else { g_sgemm_isa = 0 }
    }
    if g_sgemm_isa == 1 { sgemm_wide(A, B, C, M, N, K) }
    else { sgemm_avx2(A, B, C, M, N, K) }
}

// ============================================================================
// Cache-blocked / packed SGEMM (BLIS GEBP) — closes the per-core packing gap.
//
// The plain sgemm above reads B with stride ldb=N (a multi-MB matrix that blows
// L2), so it pays cache-miss traffic on every micro-kernel B-load. The BLIS
// "analytical model" fix (Low/Igual/Smith/Quintana-Ortí, TOMS 2016): pack A and
// B into contiguous, cache-resident panels and tile the loops so Ã stays in L2
// and a B̃ panel stays in L3. The block sizes mc/kc/nc are DERIVED from the
// detected cache sizes (not autotuned, not hardcoded) — see gemm_blocking.
// ----------------------------------------------------------------------------

// Cache-block geometry. mr×nr is the register tile (= uk_6x16). kc/mc/nc are the
// L1/L2/L3 block sizes. POD (all int), no destructor.
struct GemmBlock { int mr; int nr; int kc; int mc; int nc }

// Cached blocking geometry: the cache hierarchy is constant for the life of the
// process, so the (expensive) detection runs ONCE. mr==0 means "not yet computed"
// (a valid result always has mr==6). gemm_blocking()'s ~3.5 us cost — 6 syscalls
// to GetLogicalProcessorInformation + 3 malloc/free per call — used to be paid on
// EVERY sgemm_packed call, dwarfing small-matrix compute; memoizing pays it once.
GemmBlock g_gb = { mr: 0, nr: 0, kc: 0, mc: 0, nc: 0 }

// Derive kc/mc/nc analytically from the live cache hierarchy (c.__ls_cache_kb).
// Falls back to a Skylake-class default (32/256/8192 KB) when detection returns
// 0 — that default IS effectively a built-in CPU profile. The fractions (½ L1,
// ~56% L2, full per-core L3 slice) are the BLIS model's simplified residency
// bounds, tuned to reproduce BLIS's validated Haswell sgemm config
// (mr6 nr16 kc256 mc144) on a 32/256 KB L1/L2 machine. Detected once, then cached
// in g_gb — see the memoization guard below.
def gemm_blocking() -> GemmBlock {
    if g_gb.mr != 0 { return g_gb }       // cached: skip the cache-probe syscalls
    int l1 = c.__ls_cache_kb(1)
    int l2 = c.__ls_cache_kb(2)
    int l3 = c.__ls_cache_kb(3)
    if l1 <= 0 { l1 = 32 }
    if l2 <= 0 { l2 = 256 }
    if l3 <= 0 { l3 = 8192 }
    int ncores = c.__ls_cpu_count()
    if ncores <= 0 { ncores = 1 }

    int mr = 6
    int nr = 16
    int elem = 4                                   // f32 bytes

    // kc: the kc×nr B micro-panel should occupy ~half of L1.
    int kc = (l1 * 1024 / 2) / (nr * elem)
    // mc: the mc×kc Ã block should occupy ~56% of L2 (rest = streaming B + C).
    int mc = (l2 * 1024 * 56 / 100) / (kc * elem)
    // nc: the kc×nc B̃ panel should fit this core's slice of the shared L3.
    int l3slice = (l3 * 1024) / ncores
    int nc = l3slice / (kc * elem)

    // Snap to kernel-tile multiples; clamp to sane minimums.
    mc = ((mc + mr / 2) / mr) * mr                 // round mc to nearest mr
    nc = (nc / nr) * nr                            // floor nc to nr (conservative)
    if kc < 1 { kc = 1 }
    if mc < mr { mc = mr }
    if nc < nr { nc = nr }
    GemmBlock gb = { mr: mr, nr: nr, kc: kc, mc: mc, nc: nc }
    g_gb = gb                             // memoize for all subsequent calls
    return gb
}

// Pack a kc×nc block of B (rows [pc,pc+kcb), cols [jc,jc+ncb)) into Bt as a
// sequence of nr-wide micro-panels: panel jp holds, for each k, nr contiguous
// B[pc+k][jc+jp*nr .. +nr]. ncb is a multiple of nr (bulk region only).
def pack_b(*f32 B, int ldb, *f32 Bt, int pc, int kcb, int jc, int ncb, int nr) {
    int npanels = ncb / nr
    int jp = 0
    while jp < npanels {
        int col0 = jc + jp * nr
        int dst = jp * (kcb * nr)
        int kk = 0
        while kk < kcb {
            int lane = 0
            while lane < nr {
                Bt[dst + kk * nr + lane] = B[(pc + kk) * ldb + col0 + lane]
                lane = lane + 1
            }
            kk = kk + 1
        }
        jp = jp + 1
    }
}

// Pack an mc×kc block of A (rows [ic,ic+mcb), cols [pc,pc+kcb)) into At as a
// sequence of mr-tall micro-panels: panel ip holds, for each k, mr contiguous
// A[ic+ip*mr .. +mr][pc+k]. mcb is a multiple of mr (bulk region only).
def pack_a(*f32 A, int lda, *f32 At, int ic, int mcb, int pc, int kcb, int mr) {
    int npanels = mcb / mr
    int ip = 0
    while ip < npanels {
        int row0 = ic + ip * mr
        int dst = ip * (mr * kcb)
        int kk = 0
        while kk < kcb {
            int r = 0
            while r < mr {
                At[dst + kk * mr + r] = A[(row0 + r) * lda + pc + kk]
                r = r + 1
            }
            kk = kk + 1
        }
        ip = ip + 1
    }
}

// Packed 6×16 micro-kernel: C[ci..+6][cj..+16] += Ã_panel · B̃_panel. Reads the
// packed panels sequentially (At[aoff + k*6 + r], Bt[boff + k*16]); ACCUMULATES
// into C (loads it first) so the pc-loop sums partial kc-blocks. LS has no
// pointer arithmetic, so panel bases are passed as element offsets aoff/boff.
def uk_6x16_packed(*f32 At, int aoff, *f32 Bt, int boff,
                   *f32 C, int kc, int ldc, int ci, int cj) {
    Simd(f32, 16) c0 = __simd_load(C, (ci + 0) * ldc + cj)
    Simd(f32, 16) c1 = __simd_load(C, (ci + 1) * ldc + cj)
    Simd(f32, 16) c2 = __simd_load(C, (ci + 2) * ldc + cj)
    Simd(f32, 16) c3 = __simd_load(C, (ci + 3) * ldc + cj)
    Simd(f32, 16) c4 = __simd_load(C, (ci + 4) * ldc + cj)
    Simd(f32, 16) c5 = __simd_load(C, (ci + 5) * ldc + cj)
    // K-loop unrolled ×4: four independent B-loads and 24 FMAs per iteration give
    // the scheduler enough slack to overlap loads/broadcasts with the FMA pipe
    // (the rolled loop serialized on a single broadcast register).
    int k = 0
    int k4 = (kc / 4) * 4
    while k < k4 {
        int ab = aoff + k * 6
        Simd(f32, 16) b0 = __simd_load(Bt, boff + k * 16)
        c0 = __simd_fma(__simd_splat(At[ab + 0]), b0, c0)
        c1 = __simd_fma(__simd_splat(At[ab + 1]), b0, c1)
        c2 = __simd_fma(__simd_splat(At[ab + 2]), b0, c2)
        c3 = __simd_fma(__simd_splat(At[ab + 3]), b0, c3)
        c4 = __simd_fma(__simd_splat(At[ab + 4]), b0, c4)
        c5 = __simd_fma(__simd_splat(At[ab + 5]), b0, c5)
        Simd(f32, 16) b1 = __simd_load(Bt, boff + (k + 1) * 16)
        c0 = __simd_fma(__simd_splat(At[ab + 6]), b1, c0)
        c1 = __simd_fma(__simd_splat(At[ab + 7]), b1, c1)
        c2 = __simd_fma(__simd_splat(At[ab + 8]), b1, c2)
        c3 = __simd_fma(__simd_splat(At[ab + 9]), b1, c3)
        c4 = __simd_fma(__simd_splat(At[ab + 10]), b1, c4)
        c5 = __simd_fma(__simd_splat(At[ab + 11]), b1, c5)
        Simd(f32, 16) b2 = __simd_load(Bt, boff + (k + 2) * 16)
        c0 = __simd_fma(__simd_splat(At[ab + 12]), b2, c0)
        c1 = __simd_fma(__simd_splat(At[ab + 13]), b2, c1)
        c2 = __simd_fma(__simd_splat(At[ab + 14]), b2, c2)
        c3 = __simd_fma(__simd_splat(At[ab + 15]), b2, c3)
        c4 = __simd_fma(__simd_splat(At[ab + 16]), b2, c4)
        c5 = __simd_fma(__simd_splat(At[ab + 17]), b2, c5)
        Simd(f32, 16) b3 = __simd_load(Bt, boff + (k + 3) * 16)
        c0 = __simd_fma(__simd_splat(At[ab + 18]), b3, c0)
        c1 = __simd_fma(__simd_splat(At[ab + 19]), b3, c1)
        c2 = __simd_fma(__simd_splat(At[ab + 20]), b3, c2)
        c3 = __simd_fma(__simd_splat(At[ab + 21]), b3, c3)
        c4 = __simd_fma(__simd_splat(At[ab + 22]), b3, c4)
        c5 = __simd_fma(__simd_splat(At[ab + 23]), b3, c5)
        k = k + 4
    }
    while k < kc {
        Simd(f32, 16) b = __simd_load(Bt, boff + k * 16)
        c0 = __simd_fma(__simd_splat(At[aoff + k * 6 + 0]), b, c0)
        c1 = __simd_fma(__simd_splat(At[aoff + k * 6 + 1]), b, c1)
        c2 = __simd_fma(__simd_splat(At[aoff + k * 6 + 2]), b, c2)
        c3 = __simd_fma(__simd_splat(At[aoff + k * 6 + 3]), b, c3)
        c4 = __simd_fma(__simd_splat(At[aoff + k * 6 + 4]), b, c4)
        c5 = __simd_fma(__simd_splat(At[aoff + k * 6 + 5]), b, c5)
        k = k + 1
    }
    __simd_store(C, (ci + 0) * ldc + cj, c0)
    __simd_store(C, (ci + 1) * ldc + cj, c1)
    __simd_store(C, (ci + 2) * ldc + cj, c2)
    __simd_store(C, (ci + 3) * ldc + cj, c3)
    __simd_store(C, (ci + 4) * ldc + cj, c4)
    __simd_store(C, (ci + 5) * ldc + cj, c5)
}

// Cache-blocked SGEMM: C = A·B (overwrites C). Five-loop GEBP around uk_6x16
// with A/B packing; block sizes from gemm_blocking(). The M-divisible-by-mr ×
// N-divisible-by-nr rectangle is the packed bulk; the < mr row tail and < nr
// col tail fall back to the scalar sgemm_tail (overwrite — disjoint from bulk).
def sgemm_packed(*f32 A, *f32 B, *f32 C, int M, int N, int K) {
    GemmBlock gb = gemm_blocking()
    int mr = gb.mr
    int nr = gb.nr
    int kc = gb.kc
    int mc = gb.mc
    int nc = gb.nc

    int Mfull = (M / mr) * mr
    int Nfull = (N / nr) * nr

    // Zero C: the bulk accumulates into it across pc-blocks.
    int z = 0
    int ncell = M * N
    while z < ncell {
        C[z] = 0.0 as f32
        z = z + 1
    }

    *f32 Bt = c.malloc(kc * nc * sizeof(f32)) as *f32
    *f32 At = c.malloc(mc * kc * sizeof(f32)) as *f32

    int jc = 0
    while jc < Nfull {
        int ncb = nc
        if jc + ncb > Nfull { ncb = Nfull - jc }
        int pc = 0
        while pc < K {
            int kcb = kc
            if pc + kcb > K { kcb = K - pc }
            pack_b(B, N, Bt, pc, kcb, jc, ncb, nr)
            int ic = 0
            while ic < Mfull {
                int mcb = mc
                if ic + mcb > Mfull { mcb = Mfull - ic }
                pack_a(A, K, At, ic, mcb, pc, kcb, mr)
                int jr = 0
                while jr < ncb {
                    int jp = jr / nr
                    int ir = 0
                    while ir < mcb {
                        int ip = ir / mr
                        uk_6x16_packed(At, ip * (mr * kcb), Bt, jp * (kcb * nr), C, kcb, N, ic + ir, jc + jr)
                        ir = ir + mr
                    }
                    jr = jr + nr
                }
                ic = ic + mc
            }
            pc = pc + kc
        }
        jc = jc + nc
    }

    c.free(Bt as *u8)
    c.free(At as *u8)

    // Edges (C still zero there; overwrite with full-K dot products):
    //   N tail: rows [0,Mfull) × cols [Nfull,N)
    //   M tail: rows [Mfull,M) × cols [0,N)
    if Nfull < N { sgemm_tail(A, B, C, K, N, 0, Mfull, Nfull) }
    if Mfull < M { sgemm_tail(A, B, C, K, N, Mfull, M - Mfull, 0) }
}

// One mc×nc C-block: pack this block's A-panel into private scratch, then sweep
// the micro-kernel reading the SHARED, pre-packed B-panel Bt. Worker threads call
// this on disjoint ic-blocks (disjoint C rows), all reading the same read-only Bt
// — so B is packed ONCE, not once per thread. C must be pre-zeroed (accumulate).
def packed_ablock(*f32 A, *f32 Bt, *f32 C, int N, int K, int ic, int mcb,
                  int pc, int kcb, int jc, int ncb, int mr, int nr, int mc, int kc) {
    *f32 At = c.malloc(mc * kc * sizeof(f32)) as *f32
    pack_a(A, K, At, ic, mcb, pc, kcb, mr)
    int jr = 0
    while jr < ncb {
        int jp = jr / nr
        int ir = 0
        while ir < mcb {
            int ip = ir / mr
            uk_6x16_packed(At, ip * (mr * kcb), Bt, jp * (kcb * nr), C, kcb, N, ic + ir, jc + jr)
            ir = ir + mr
        }
        jr = jr + nr
    }
    c.free(At as *u8)
}

// Multithreaded packed SGEMM (BLIS parallelization of the ic loop): for each
// (jc, pc) panel, pack B ONCE (serial), then fan the mc-row-blocks across cores
// — each worker packs its own A-block and runs the micro-kernel against the
// shared B̃. Avoids the redundant per-thread B-packing that saturates memory
// bandwidth. Row/col tails finish serially.
def sgemm_packed_mt(*f32 A, *f32 B, *f32 C, int M, int N, int K) {
    GemmBlock gb = gemm_blocking()
    int mr = gb.mr
    int nr = gb.nr
    int kc = gb.kc
    int mc = gb.mc
    int nc = gb.nc
    int Mfull = (M / mr) * mr
    int Nfull = (N / nr) * nr

    int z = 0
    int ncell = M * N
    while z < ncell {
        C[z] = 0.0 as f32
        z = z + 1
    }

    *f32 Bt = c.malloc(kc * nc * sizeof(f32)) as *f32
    int jc = 0
    while jc < Nfull {
        int ncb = nc
        if jc + ncb > Nfull { ncb = Nfull - jc }
        int pc = 0
        while pc < K {
            int kcb = kc
            if pc + kcb > K { kcb = K - pc }
            pack_b(B, N, Bt, pc, kcb, jc, ncb, nr)        // ONCE, shared
            int nblk = (Mfull + mc - 1) / mc
            thread.parallel_for(0, nblk, |ib| {
                int ic = ib * mc
                int mcb = mc
                if ic + mcb > Mfull { mcb = Mfull - ic }
                packed_ablock(A, Bt, C, N, K, ic, mcb, pc, kcb, jc, ncb, mr, nr, mc, kc)
            })
            pc = pc + kc
        }
        jc = jc + nc
    }
    c.free(Bt as *u8)

    if Nfull < N { sgemm_tail(A, B, C, K, N, 0, Mfull, Nfull) }
    if Mfull < M { sgemm_tail(A, B, C, K, N, Mfull, M - Mfull, 0) }
}

// ----------------------------------------------------------------------------
// F16-storage / F32-accumulate SGEMM (C2, the precision-safe inference path):
// A and B are f16 (half the memory bandwidth of f32 — the real win for
// memory-bound batch=1 inference), but every product accumulates in f32 so the
// K-reduction keeps full precision (native f16 accumulation loses ~11-bit
// mantissa over K and is unsafe). Each step: load a 16-wide f16 B-slice and widen
// to f32 (vcvtph2ps), widen the f16 A-scalar (`as f32`) and broadcast, FMA in
// f32. Accumulators and output C are f32 (16-wide), so the tile geometry mirrors
// the f32 kernels exactly — only the loads differ. 6×16 workhorse + 1×16 tail +
// scalar N-tail; wider 12×16/12×32 f16 variants are mechanical mirrors (same f32
// accumulators, f16 loads) and omitted here.

// A-broadcast and B-load both go f16→f32 via vector ops only (vpbroadcastw +
// vcvtph2ps / F16C) — NO scalar __extendhfsf2 libcall, so the kernel links under
// AOT (scalar `f16 as f32` would pull in compiler-rt soft-float). `full`/`rem`
// pick a full 16-wide tile or a masked tail tile (cols past the last mult of 16).
def uk_6x16_f16(*f16 A, *f16 B, *f32 C, int K, int lda, int ldb, int ldc,
                int ai, int bj, int rem, bool full) {
    Simd(f32, 16) c0 = __simd_zero()
    Simd(f32, 16) c1 = __simd_zero()
    Simd(f32, 16) c2 = __simd_zero()
    Simd(f32, 16) c3 = __simd_zero()
    Simd(f32, 16) c4 = __simd_zero()
    Simd(f32, 16) c5 = __simd_zero()
    int k = 0
    while k < K {
        Simd(f16, 16) bh = __simd_zero()
        if full { bh = __simd_load(B, k * ldb + bj) }
        else { bh = __simd_load_masked(B, k * ldb + bj, rem) }
        Simd(f32, 16) b = __simd_cast(bh)
        c0 = __simd_fma(__simd_cast(__simd_splat(A[(ai + 0) * lda + k])), b, c0)
        c1 = __simd_fma(__simd_cast(__simd_splat(A[(ai + 1) * lda + k])), b, c1)
        c2 = __simd_fma(__simd_cast(__simd_splat(A[(ai + 2) * lda + k])), b, c2)
        c3 = __simd_fma(__simd_cast(__simd_splat(A[(ai + 3) * lda + k])), b, c3)
        c4 = __simd_fma(__simd_cast(__simd_splat(A[(ai + 4) * lda + k])), b, c4)
        c5 = __simd_fma(__simd_cast(__simd_splat(A[(ai + 5) * lda + k])), b, c5)
        k = k + 1
    }
    if full {
        __simd_store(C, (ai + 0) * ldc + bj, c0)
        __simd_store(C, (ai + 1) * ldc + bj, c1)
        __simd_store(C, (ai + 2) * ldc + bj, c2)
        __simd_store(C, (ai + 3) * ldc + bj, c3)
        __simd_store(C, (ai + 4) * ldc + bj, c4)
        __simd_store(C, (ai + 5) * ldc + bj, c5)
    } else {
        __simd_store_masked(C, (ai + 0) * ldc + bj, c0, rem)
        __simd_store_masked(C, (ai + 1) * ldc + bj, c1, rem)
        __simd_store_masked(C, (ai + 2) * ldc + bj, c2, rem)
        __simd_store_masked(C, (ai + 3) * ldc + bj, c3, rem)
        __simd_store_masked(C, (ai + 4) * ldc + bj, c4, rem)
        __simd_store_masked(C, (ai + 5) * ldc + bj, c5, rem)
    }
}

def uk_1x16_f16(*f16 A, *f16 B, *f32 C, int K, int lda, int ldb, int ldc,
                int ai, int bj, int rem, bool full) {
    Simd(f32, 16) c0 = __simd_zero()
    int k = 0
    while k < K {
        Simd(f16, 16) bh = __simd_zero()
        if full { bh = __simd_load(B, k * ldb + bj) }
        else { bh = __simd_load_masked(B, k * ldb + bj, rem) }
        Simd(f32, 16) b = __simd_cast(bh)
        c0 = __simd_fma(__simd_cast(__simd_splat(A[ai * lda + k])), b, c0)
        k = k + 1
    }
    if full { __simd_store(C, ai * ldc + bj, c0) }
    else { __simd_store_masked(C, ai * ldc + bj, c0, rem) }
}

// C(f32) = A(f16) · B(f16), row-major, f32 accumulate. C is overwritten.
// 6×16 workhorse + 1×16 M-tail; each N row covers full 16-wide tiles then one
// masked tail tile (no scalar f16 path → AOT-clean).
def sgemm_f16(*f16 A, *f16 B, *f32 C, int M, int N, int K) {
    int nfull = (N / 16) * 16
    int i = 0
    while i + 6 <= M {
        int j = 0
        while j < nfull { uk_6x16_f16(A, B, C, K, K, N, N, i, j, 16, true); j = j + 16 }
        if nfull < N { uk_6x16_f16(A, B, C, K, K, N, N, i, nfull, N - nfull, false) }
        i = i + 6
    }
    while i < M {
        int j = 0
        while j < nfull { uk_1x16_f16(A, B, C, K, K, N, N, i, j, 16, true); j = j + 16 }
        if nfull < N { uk_1x16_f16(A, B, C, K, K, N, N, i, nfull, N - nfull, false) }
        i = i + 1
    }
}

// ============================================================================
// 1D convolution via im2col + sgemm.
//
// A conv is a GEMM in disguise: unfold each output position's receptive field
// into a column (im2col), then weights · columns IS the convolution. Reusing
// sgemm gives the register-blocked SIMD kernel for free; only the unfold +
// bias are conv-specific.
//
// Layout (channel-major, "same"/valid via pad): input is [Cin][W], weights are
// [Cout][Cin*K] (row co = the flattened receptive field), output is [Cout][Wout]
// with Wout = W + 2*pad - K + 1 (stride 1). `col` is caller-provided scratch of
// Cin*K * Wout f32 (carve it from a Pool) so conv1d does zero allocation — the
// hard-real-time contract. The wider win (NWc16 channel-blocked layout, direct
// channel-vectorized conv) is a perf layout deferred behind this correct form.
def conv1d(*f32 input, *f32 weights, *f32 bias, *f32 output, *f32 col,
           int Cin, int Cout, int W, int K, int pad) {
    int Wout = W + 2 * pad - K + 1
    // im2col: col[(cin*K + k)][ow] = input[cin][ow - pad + k]  (0 outside [0,W))
    int cin = 0
    while cin < Cin {
        int k = 0
        while k < K {
            int row = cin * K + k
            int ow = 0
            while ow < Wout {
                int iw = ow - pad + k
                f32 val = 0.0 as f32
                if iw >= 0 && iw < W { val = input[cin * W + iw] }
                col[row * Wout + ow] = val
                ow = ow + 1
            }
            k = k + 1
        }
        cin = cin + 1
    }
    // output[Cout x Wout] = weights[Cout x (Cin*K)] · col[(Cin*K) x Wout]
    sgemm(weights, col, output, Cout, Wout, Cin * K)
    // per-output-channel bias
    int co = 0
    while co < Cout {
        int ow = 0
        while ow < Wout {
            output[co * Wout + ow] = output[co * Wout + ow] + bias[co]
            ow = ow + 1
        }
        co = co + 1
    }
}

// 2D convolution via im2col + sgemm (the conv1d pattern in two spatial dims).
// input is [Cin][H][W], weights are [Cout][Cin*Kh*Kw] (row co = flattened
// receptive field), output is [Cout][Hout*Wout] with Hout = H+2*pad-Kh+1,
// Wout = W+2*pad-Kw+1 (stride 1). `col` is caller scratch of Cin*Kh*Kw * Hout*Wout
// f32 (carve from a Pool for zero alloc on the hot path). Reuses sgemm for the
// register-blocked SIMD kernel; only the unfold + bias are conv-specific.
def conv2d(*f32 input, *f32 weights, *f32 bias, *f32 output, *f32 col,
           int Cin, int Cout, int H, int W, int Kh, int Kw, int pad) {
    int Hout = H + 2 * pad - Kh + 1
    int Wout = W + 2 * pad - Kw + 1
    int OHW = Hout * Wout
    // im2col: col[cin*Kh*Kw + kh*Kw + kw][oh*Wout+ow] = input[cin][oh-pad+kh][ow-pad+kw]
    int cin = 0
    while cin < Cin {
        int kh = 0
        while kh < Kh {
            int kw = 0
            while kw < Kw {
                int row = (cin * Kh + kh) * Kw + kw
                int oh = 0
                while oh < Hout {
                    int ow = 0
                    while ow < Wout {
                        int ih = oh - pad + kh
                        int iw = ow - pad + kw
                        f32 val = 0.0 as f32
                        if ih >= 0 && ih < H && iw >= 0 && iw < W {
                            val = input[(cin * H + ih) * W + iw]
                        }
                        col[row * OHW + oh * Wout + ow] = val
                        ow = ow + 1
                    }
                    oh = oh + 1
                }
                kw = kw + 1
            }
            kh = kh + 1
        }
        cin = cin + 1
    }
    // output[Cout x OHW] = weights[Cout x (Cin*Kh*Kw)] · col[(Cin*Kh*Kw) x OHW]
    sgemm(weights, col, output, Cout, OHW, Cin * Kh * Kw)
    // per-output-channel bias
    int co = 0
    while co < Cout {
        int p = 0
        while p < OHW { output[co * OHW + p] = output[co * OHW + p] + bias[co]; p = p + 1 }
        co = co + 1
    }
}

// ----------------------------------------------------------------------------
// Direct (im2col-free) convolution — C2. The im2col path materializes a col
// matrix ~K× the input and rearranges it with a SCALAR gather (pure data
// movement, no FLOP) before the sgemm. The direct kernel instead vectorizes
// over the OUTPUT WIDTH (16-wide tiles): the accumulator holds 16 consecutive
// output positions across the whole (cin, k) reduction; each step broadcasts one
// weight scalar and FMAs it against a contiguous 16-wide input slice. Padding is
// handled by ONE zero-padded input copy (size Cin·Wp ≪ the im2col Cin·K·Wout),
// so every load is in-bounds and contiguous — no per-tap bounds branch, no col
// blow-up, packed FMA emitted directly. Same layout / bit-identical result as
// conv1d/conv2d (integer inputs ⇒ exact; SDE: scalar gather eliminated, FMA
// count unchanged = op_flops/32).

// conv1d direct. `pbuf` is caller scratch of Cin*(W+2*pad) f32 (carve from a
// Pool); the masked tail reads only `rem` lanes so no over-read past pbuf.
//
// Register-blocked over OUTPUT CHANNELS (6 at a time) so one 16-wide input load
// feeds 6 FMAs — same arithmetic intensity as the sgemm 6×16 micro-kernel, but
// reading the receptive field straight from the padded input (no im2col col).
// A 1-channel tail handles Cout % 6. Best when Wout >= 16 (full SIMD lanes); for
// Wout < 16 prefer im2col (it flattens Hout*Wout to keep lanes full).
def conv1d_direct(*f32 input, *f32 weights, *f32 bias, *f32 output, *f32 pbuf,
                  int Cin, int Cout, int W, int K, int pad) {
    int Wp = W + 2 * pad
    int Wout = W + 2 * pad - K + 1
    int CK = Cin * K
    // zero-padded input copy [Cin][Wp]
    int ci = 0
    while ci < Cin {
        int j = 0
        while j < Wp { pbuf[ci * Wp + j] = 0.0 as f32; j = j + 1 }
        int w0 = 0
        while w0 < W { pbuf[ci * Wp + pad + w0] = input[ci * W + w0]; w0 = w0 + 1 }
        ci = ci + 1
    }
    int co = 0
    // 6-output-channel blocks
    while co + 6 <= Cout {
        int r0 = co * CK
        int r1 = (co + 1) * CK
        int r2 = (co + 2) * CK
        int r3 = (co + 3) * CK
        int r4 = (co + 4) * CK
        int r5 = (co + 5) * CK
        int ow = 0
        while ow < Wout {
            int rem = Wout - ow
            bool full = rem >= 16
            Simd(f32, 16) a0 = __simd_splat(bias[co + 0])
            Simd(f32, 16) a1 = __simd_splat(bias[co + 1])
            Simd(f32, 16) a2 = __simd_splat(bias[co + 2])
            Simd(f32, 16) a3 = __simd_splat(bias[co + 3])
            Simd(f32, 16) a4 = __simd_splat(bias[co + 4])
            Simd(f32, 16) a5 = __simd_splat(bias[co + 5])
            int cin = 0
            while cin < Cin {
                int ib = cin * Wp + ow
                int wb = cin * K
                int k = 0
                while k < K {
                    Simd(f32, 16) vx = __simd_zero()
                    if full { vx = __simd_load(pbuf, ib + k) }
                    else { vx = __simd_load_masked(pbuf, ib + k, rem) }
                    a0 = __simd_fma(__simd_splat(weights[r0 + wb + k]), vx, a0)
                    a1 = __simd_fma(__simd_splat(weights[r1 + wb + k]), vx, a1)
                    a2 = __simd_fma(__simd_splat(weights[r2 + wb + k]), vx, a2)
                    a3 = __simd_fma(__simd_splat(weights[r3 + wb + k]), vx, a3)
                    a4 = __simd_fma(__simd_splat(weights[r4 + wb + k]), vx, a4)
                    a5 = __simd_fma(__simd_splat(weights[r5 + wb + k]), vx, a5)
                    k = k + 1
                }
                cin = cin + 1
            }
            if full {
                __simd_store(output, (co + 0) * Wout + ow, a0)
                __simd_store(output, (co + 1) * Wout + ow, a1)
                __simd_store(output, (co + 2) * Wout + ow, a2)
                __simd_store(output, (co + 3) * Wout + ow, a3)
                __simd_store(output, (co + 4) * Wout + ow, a4)
                __simd_store(output, (co + 5) * Wout + ow, a5)
            } else {
                __simd_store_masked(output, (co + 0) * Wout + ow, a0, rem)
                __simd_store_masked(output, (co + 1) * Wout + ow, a1, rem)
                __simd_store_masked(output, (co + 2) * Wout + ow, a2, rem)
                __simd_store_masked(output, (co + 3) * Wout + ow, a3, rem)
                __simd_store_masked(output, (co + 4) * Wout + ow, a4, rem)
                __simd_store_masked(output, (co + 5) * Wout + ow, a5, rem)
            }
            ow = ow + 16
        }
        co = co + 6
    }
    // output-channel tail (< 6 left), one channel at a time
    while co < Cout {
        int rbase = co * CK
        int ow = 0
        while ow < Wout {
            int rem = Wout - ow
            bool full = rem >= 16
            Simd(f32, 16) acc = __simd_splat(bias[co])
            int cin = 0
            while cin < Cin {
                int ib = cin * Wp + ow
                int wb = cin * K
                int k = 0
                while k < K {
                    Simd(f32, 16) vx = __simd_zero()
                    if full { vx = __simd_load(pbuf, ib + k) }
                    else { vx = __simd_load_masked(pbuf, ib + k, rem) }
                    acc = __simd_fma(__simd_splat(weights[rbase + wb + k]), vx, acc)
                    k = k + 1
                }
                cin = cin + 1
            }
            if full { __simd_store(output, co * Wout + ow, acc) }
            else { __simd_store_masked(output, co * Wout + ow, acc, rem) }
            ow = ow + 16
        }
        co = co + 1
    }
}

// conv2d direct. `pbuf` is caller scratch of Cin*(H+2*pad)*(W+2*pad) f32.
// Register-blocked over OUTPUT CHANNELS (6) like conv1d_direct: one 16-wide
// input slice feeds 6 FMAs. Best at Wout >= 16; for Wout < 16 use im2col.
def conv2d_direct(*f32 input, *f32 weights, *f32 bias, *f32 output, *f32 pbuf,
                  int Cin, int Cout, int H, int W, int Kh, int Kw, int pad) {
    int Hp = H + 2 * pad
    int Wp = W + 2 * pad
    int Hout = H + 2 * pad - Kh + 1
    int Wout = W + 2 * pad - Kw + 1
    int CKK = Cin * Kh * Kw
    // zero-padded input copy [Cin][Hp][Wp]
    int ci = 0
    while ci < Cin {
        int j = 0
        while j < Hp * Wp { pbuf[ci * Hp * Wp + j] = 0.0 as f32; j = j + 1 }
        int ih = 0
        while ih < H {
            int iw = 0
            while iw < W {
                pbuf[(ci * Hp + pad + ih) * Wp + pad + iw] = input[(ci * H + ih) * W + iw]
                iw = iw + 1
            }
            ih = ih + 1
        }
        ci = ci + 1
    }
    int co = 0
    // 6-output-channel blocks
    while co + 6 <= Cout {
        int r0 = co * CKK
        int r1 = (co + 1) * CKK
        int r2 = (co + 2) * CKK
        int r3 = (co + 3) * CKK
        int r4 = (co + 4) * CKK
        int r5 = (co + 5) * CKK
        int oh = 0
        while oh < Hout {
            int ow = 0
            while ow < Wout {
                int rem = Wout - ow
                bool full = rem >= 16
                Simd(f32, 16) a0 = __simd_splat(bias[co + 0])
                Simd(f32, 16) a1 = __simd_splat(bias[co + 1])
                Simd(f32, 16) a2 = __simd_splat(bias[co + 2])
                Simd(f32, 16) a3 = __simd_splat(bias[co + 3])
                Simd(f32, 16) a4 = __simd_splat(bias[co + 4])
                Simd(f32, 16) a5 = __simd_splat(bias[co + 5])
                int cin = 0
                while cin < Cin {
                    int wb = cin * Kh * Kw
                    int kh = 0
                    while kh < Kh {
                        int ibr = (cin * Hp + oh + kh) * Wp + ow
                        int wbr = wb + kh * Kw
                        int kw = 0
                        while kw < Kw {
                            Simd(f32, 16) vx = __simd_zero()
                            if full { vx = __simd_load(pbuf, ibr + kw) }
                            else { vx = __simd_load_masked(pbuf, ibr + kw, rem) }
                            a0 = __simd_fma(__simd_splat(weights[r0 + wbr + kw]), vx, a0)
                            a1 = __simd_fma(__simd_splat(weights[r1 + wbr + kw]), vx, a1)
                            a2 = __simd_fma(__simd_splat(weights[r2 + wbr + kw]), vx, a2)
                            a3 = __simd_fma(__simd_splat(weights[r3 + wbr + kw]), vx, a3)
                            a4 = __simd_fma(__simd_splat(weights[r4 + wbr + kw]), vx, a4)
                            a5 = __simd_fma(__simd_splat(weights[r5 + wbr + kw]), vx, a5)
                            kw = kw + 1
                        }
                        kh = kh + 1
                    }
                    cin = cin + 1
                }
                if full {
                    __simd_store(output, ((co + 0) * Hout + oh) * Wout + ow, a0)
                    __simd_store(output, ((co + 1) * Hout + oh) * Wout + ow, a1)
                    __simd_store(output, ((co + 2) * Hout + oh) * Wout + ow, a2)
                    __simd_store(output, ((co + 3) * Hout + oh) * Wout + ow, a3)
                    __simd_store(output, ((co + 4) * Hout + oh) * Wout + ow, a4)
                    __simd_store(output, ((co + 5) * Hout + oh) * Wout + ow, a5)
                } else {
                    __simd_store_masked(output, ((co + 0) * Hout + oh) * Wout + ow, a0, rem)
                    __simd_store_masked(output, ((co + 1) * Hout + oh) * Wout + ow, a1, rem)
                    __simd_store_masked(output, ((co + 2) * Hout + oh) * Wout + ow, a2, rem)
                    __simd_store_masked(output, ((co + 3) * Hout + oh) * Wout + ow, a3, rem)
                    __simd_store_masked(output, ((co + 4) * Hout + oh) * Wout + ow, a4, rem)
                    __simd_store_masked(output, ((co + 5) * Hout + oh) * Wout + ow, a5, rem)
                }
                ow = ow + 16
            }
            oh = oh + 1
        }
        co = co + 6
    }
    // output-channel tail (< 6 left)
    while co < Cout {
        int rbase = co * CKK
        int oh = 0
        while oh < Hout {
            int ow = 0
            while ow < Wout {
                int rem = Wout - ow
                bool full = rem >= 16
                Simd(f32, 16) acc = __simd_splat(bias[co])
                int cin = 0
                while cin < Cin {
                    int wb = cin * Kh * Kw
                    int kh = 0
                    while kh < Kh {
                        int ibr = (cin * Hp + oh + kh) * Wp + ow
                        int wbr = wb + kh * Kw
                        int kw = 0
                        while kw < Kw {
                            Simd(f32, 16) vx = __simd_zero()
                            if full { vx = __simd_load(pbuf, ibr + kw) }
                            else { vx = __simd_load_masked(pbuf, ibr + kw, rem) }
                            acc = __simd_fma(__simd_splat(weights[rbase + wbr + kw]), vx, acc)
                            kw = kw + 1
                        }
                        kh = kh + 1
                    }
                    cin = cin + 1
                }
                if full { __simd_store(output, (co * Hout + oh) * Wout + ow, acc) }
                else { __simd_store_masked(output, (co * Hout + oh) * Wout + ow, acc, rem) }
                ow = ow + 16
            }
            oh = oh + 1
        }
        co = co + 1
    }
}

// 2D max pooling. input [C][H][W] -> output [C][Hout][Wout], Hout=(H-k)/s+1.
def maxpool2d(*f32 input, *f32 output, int C, int H, int W, int k, int s) {
    int Hout = (H - k) / s + 1
    int Wout = (W - k) / s + 1
    int c = 0
    while c < C {
        int oh = 0
        while oh < Hout {
            int ow = 0
            while ow < Wout {
                f32 m = input[(c * H + oh * s) * W + ow * s]
                int ph = 0
                while ph < k {
                    int pw = 0
                    while pw < k {
                        f32 v = input[(c * H + oh * s + ph) * W + ow * s + pw]
                        if v > m { m = v }
                        pw = pw + 1
                    }
                    ph = ph + 1
                }
                output[(c * Hout + oh) * Wout + ow] = m
                ow = ow + 1
            }
            oh = oh + 1
        }
        c = c + 1
    }
}

// 2D average pooling. Same windowing as maxpool2d; f64 accumulation.
def avgpool2d(*f32 input, *f32 output, int C, int H, int W, int k, int s) {
    int Hout = (H - k) / s + 1
    int Wout = (W - k) / s + 1
    f64 area = (k * k) as f64
    int c = 0
    while c < C {
        int oh = 0
        while oh < Hout {
            int ow = 0
            while ow < Wout {
                f64 sum = 0.0
                int ph = 0
                while ph < k {
                    int pw = 0
                    while pw < k {
                        sum = sum + (input[(c * H + oh * s + ph) * W + ow * s + pw] as f64)
                        pw = pw + 1
                    }
                    ph = ph + 1
                }
                output[(c * Hout + oh) * Wout + ow] = (sum / area) as f32
                ow = ow + 1
            }
            oh = oh + 1
        }
        c = c + 1
    }
}

// Global average pooling. input [C][H][W] -> output [C] (one mean per channel).
def gap(*f32 input, *f32 output, int C, int H, int W) {
    int hw = H * W
    f64 area = hw as f64
    int c = 0
    while c < C {
        f64 sum = 0.0
        int p = 0
        while p < hw { sum = sum + (input[c * hw + p] as f64); p = p + 1 }
        output[c] = (sum / area) as f32
        c = c + 1
    }
}

// Elementwise ReLU over n contiguous f32, in place. 16-wide body + masked tail
// (the non-multiple-of-16 fringe), so any n works with no scalar loop.
def relu_inplace(*f32 p, int n) {
    Simd(f32, 16) z = __simd_zero()
    int i = 0
    while i + 16 <= n {
        Simd(f32, 16) v = __simd_load(p, i)
        __simd_store(p, i, __simd_max(v, z))
        i = i + 16
    }
    int rem = n - i
    if rem > 0 {
        Simd(f32, 16) v = __simd_load_masked(p, i, rem)
        __simd_store_masked(p, i, __simd_max(v, z), rem)
    }
}

// SiLU / Swish activation in place over n f32: x * sigmoid(x). Fully
// vectorized (smd.silu uses the SIMD tanh approximation) +
// masked tail; same shape as gelu_inplace.
def silu_inplace(*f32 p, int n) {
    int i = 0
    while i + 16 <= n {
        Simd(f32, 16) v = __simd_load(p, i)
        __simd_store(p, i, smd.silu(v))
        i = i + 16
    }
    int rem = n - i
    if rem > 0 {
        Simd(f32, 16) v = __simd_load_masked(p, i, rem)
        __simd_store_masked(p, i, smd.silu(v), rem)
    }
}

// Standalone Nd BatchNorm (inference) in place over a channel-major [C][S]
// tensor: y[c, :] = scale[c] * x[c, :] + bias[c], where the caller pre-folds the
// running stats into per-channel affine: scale[c] = gamma[c]/sqrt(var[c]+eps),
// bias[c] = beta[c] - mean[c]*scale[c]. Each channel is a contiguous S-length row;
// scale[c]/bias[c] are broadcast and FMA'd over S (vectorized + masked tail).
// Used where BN is followed by SiLU (so it can't fold into a preceding gemm —
// unlike the folded bn epilogue on the MLP/CNN gemm path).
def batchnorm_affine(*f32 p, *f32 scale, *f32 bias, int C, int S) {
    int c = 0
    while c < C {
        Simd(f32, 16) vs = __simd_splat(scale[c])
        Simd(f32, 16) vb = __simd_splat(bias[c])
        int base = c * S
        int i = 0
        while i + 16 <= S {
            Simd(f32, 16) v = __simd_load(p, base + i)
            __simd_store(p, base + i, __simd_fma(v, vs, vb))
            i = i + 16
        }
        int rem = S - i
        if rem > 0 {
            Simd(f32, 16) v = __simd_load_masked(p, base + i, rem)
            __simd_store_masked(p, base + i, __simd_fma(v, vs, vb), rem)
        }
        c = c + 1
    }
}

// Strided depthwise 1D conv — convs axis `a` of a tensor viewed as
// [C][outer][L][inner] DIRECTLY (no permute): outer = ∏ dims before a,
// L = d_a (conv axis), inner = ∏ dims after a (contiguous). w is [C][k].
// Vectorizes over the contiguous `inner` (broadcast each weight tap, FMA). This
// is what an axis-separable CNN's per-axis convs use — operating in place on the
// natural layout avoids permute traffic entirely (a model with ~80 such convs
// would otherwise permute its full activation 160× — the memory-bound killer).
// Padding via a per-(c,o) [L+2pad][inner] scratch: zero the
// two pad bands + ONE memcpy of the L*inner middle. `pbuf` = (L+2*pad)*inner f32.
//
// Three shape-dispatched paths (Phase-B perf, measured on representative conv axes):
//  (1) k==1, pad==0  -> the conv is a per-channel elementwise scale out=w[c]*in;
//      run it fully contiguous & 16-wide over the whole channel block (the `pol`
//      axis: inner=1 under the generic path wasted 15/16 lanes + did one tiny
//      memcpy per (c,o) = millions of FFI calls -> 28ms; this path ~1ms).
//  (2) inner < 16    -> NO scratch: inline boundary test, one masked op per (o,ol).
//      The masked compute still under-fills lanes, but it kills the per-(c,o)
//      scratch memcpy/zero that dominates these small-(L*inner) axes (col/row).
//  (3) inner >= 16   -> scratch + inner-vectorize (well-filled, the original path).
def dwconv1d_strided(*f32 inp, *f32 w, *f32 out, *f32 pbuf,
                     int C, int outer, int L, int inner, int k, int pad) {
    int Lp = L + 2 * pad
    int Lout = L + 2 * pad - k + 1
    int padN = pad * inner
    // (1) k==1 pad==0: elementwise per-channel scale, contiguous 16-wide.
    if k == 1 && pad == 0 {
        int n = outer * L * inner
        Simd(f32, 16) zr = __simd_zero()
        int co1 = 0
        while co1 < C {
            Simd(f32, 16) vw = __simd_splat(w[co1])
            int base = co1 * n
            int i = 0
            while i + 16 <= n {
                Simd(f32, 16) vx = __simd_load(inp, base + i)
                __simd_store(out, base + i, __simd_fma(vw, vx, zr))
                i = i + 16
            }
            int rem = n - i
            if rem > 0 {
                Simd(f32, 16) vx = __simd_load_masked(inp, base + i, rem)
                __simd_store_masked(out, base + i, __simd_fma(vw, vx, zr), rem)
            }
            co1 = co1 + 1
        }
        return
    }
    // (2a) inner < 16, same conv (k == 2*pad+1, Lout==L): LANE-FILL. The masked
    // per-(o,ol) path wasted lanes (col inner=2 -> 2/16). Instead build a per-channel
    // padded buffer in pbuf (front/back pad*inner zeros so shifted loads stay in
    // bounds), then vectorize the conv over the *contiguous* output q in full 16-wide
    // chunks: out[q] = Σ_j w[j] * pbuf[q+j*inner] * mask_j[q]. mask_j is a precomputed
    // 0/1 vector that zeros lanes whose (l-pad+j) crosses an outer's [0,L) boundary
    // (period mp = lcm(L*inner,16); multiply-by-0/1 needs no blend intrinsic). pbuf
    // layout: [pad*inner front][D=outer*L*inner data][pad*inner back][k*mp masks].
    if inner < 16 && k == 2 * pad + 1 {
        int D = outer * L * inner
        int F = pad * inner
        int mp = L * inner
        while mp % 16 != 0 { mp = mp + L * inner }
        int maskbase = D + 2 * F
        int jj = 0
        while jj < k {
            int p = 0
            while p < mp {
                int l = (p / inner) % L
                int t = l - pad + jj
                f32 mv = 0.0 as f32
                if t >= 0 && t < L { mv = 1.0 as f32 }
                pbuf[maskbase + jj * mp + p] = mv
                p = p + 1
            }
            jj = jj + 1
        }
        int co2 = 0
        while co2 < C {
            int wb = co2 * k
            int z = 0
            while z < F { pbuf[z] = 0.0 as f32; pbuf[F + D + z] = 0.0 as f32; z = z + 1 }
            c.__ls_bytecopy(pbuf as *u8, F * 4, inp as *u8, (co2 * D) * 4, D * 4)
            int q = 0
            while q + 16 <= D {
                Simd(f32, 16) acc = __simd_zero()
                int j = 0
                while j < k {
                    Simd(f32, 16) vx = __simd_load(pbuf, q + j * inner)
                    Simd(f32, 16) vm = __simd_load(pbuf, maskbase + j * mp + (q % mp))
                    acc = __simd_fma(__simd_splat(w[wb + j]), vx * vm, acc)
                    j = j + 1
                }
                __simd_store(out, co2 * D + q, acc)
                q = q + 16
            }
            int rem = D - q
            if rem > 0 {
                Simd(f32, 16) acc = __simd_zero()
                int j = 0
                while j < k {
                    Simd(f32, 16) vx = __simd_load_masked(pbuf, q + j * inner, rem)
                    Simd(f32, 16) vm = __simd_load(pbuf, maskbase + j * mp + (q % mp))
                    acc = __simd_fma(__simd_splat(w[wb + j]), vx * vm, acc)
                    j = j + 1
                }
                __simd_store_masked(out, co2 * D + q, acc, rem)
            }
            co2 = co2 + 1
        }
        return
    }
    // (2b) inner < 16, non-"same" conv: masked SIMD per-(o,ol), inline boundary.
    if inner < 16 {
        int co2 = 0
        while co2 < C {
            int wb = co2 * k
            int o = 0
            while o < outer {
                int bi = (co2 * outer + o) * L * inner
                int bo = (co2 * outer + o) * Lout * inner
                int ol = 0
                while ol < Lout {
                    Simd(f32, 16) acc = __simd_zero()
                    int j = 0
                    while j < k {
                        int il = ol - pad + j
                        if il >= 0 && il < L {
                            Simd(f32, 16) vx = __simd_load_masked(inp, bi + il * inner, inner)
                            acc = __simd_fma(__simd_splat(w[wb + j]), vx, acc)
                        }
                        j = j + 1
                    }
                    __simd_store_masked(out, bo + ol * inner, acc, inner)
                    ol = ol + 1
                }
                o = o + 1
            }
            co2 = co2 + 1
        }
        return
    }
    // (3) inner >= 16: scratch + inner-vectorize (original well-filled path).
    int co = 0
    while co < C {
        int wbase = co * k
        int o = 0
        while o < outer {
            int base_in = (co * outer + o) * L * inner
            int base_out = (co * outer + o) * Lout * inner
            // pad bands -> 0, middle <- one memcpy of L*inner contiguous
            int z = 0
            while z < padN {
                pbuf[z] = 0.0 as f32
                pbuf[(pad + L) * inner + z] = 0.0 as f32
                z = z + 1
            }
            c.__ls_bytecopy(pbuf as *u8, padN * 4, inp as *u8, base_in * 4, L * inner * 4)
            int ol = 0
            while ol < Lout {
                int ob = base_out + ol * inner
                int i = 0
                while i + 16 <= inner {
                    Simd(f32, 16) acc = __simd_zero()
                    int j = 0
                    while j < k {
                        Simd(f32, 16) vx = __simd_load(pbuf, (ol + j) * inner + i)
                        acc = __simd_fma(__simd_splat(w[wbase + j]), vx, acc)
                        j = j + 1
                    }
                    __simd_store(out, ob + i, acc)
                    i = i + 16
                }
                int rem = inner - i
                if rem > 0 {
                    Simd(f32, 16) acc = __simd_zero()
                    int j = 0
                    while j < k {
                        Simd(f32, 16) vx = __simd_load_masked(pbuf, (ol + j) * inner + i, rem)
                        acc = __simd_fma(__simd_splat(w[wbase + j]), vx, acc)
                        j = j + 1
                    }
                    __simd_store_masked(out, ob + i, acc, rem)
                }
                ol = ol + 1
            }
            o = o + 1
        }
        co = co + 1
    }
}

// ============================================================================
// Transformer Tier-B/C kernels (P5-2): softmax / layernorm / gelu / transpose /
// add. softmax & layernorm reduce per row in f64 (numerically-stable max-shift /
// mean-var) — math.exp / math.sqrt are scalar here; a SIMD exp / rsqrt is the
// deferred perf win (simd.ls notes exp is Tier-2). gelu uses the SIMD tanh
// approximation; add is fully vectorized. Correctness-first, mirroring the f64
// bn_f32 epilogue pattern from the MLP/CNN path.

// Row-wise softmax over a row-major [rows, cols] f32 matrix, in place. Each row:
// subtract row-max (numerical stability), exp, normalize by the row sum.
// Vectorized: exp via smd.exp over 16-wide column chunks (+ scalar f64 tail for
// cols % 16). Rows with cols < 16 fall fully into the scalar path (== old f64
// behavior). f32 vector accumulation is fine for typical softmax widths.
def softmax_rows(*f32 p, int rows, int cols) {
    int i = 0
    while i < rows {
        int base = i * cols
        // row max (f32)
        f32 mx = p[base]
        int j = 1
        while j < cols { if p[base + j] > mx { mx = p[base + j] } j = j + 1 }
        // exp(x - mx) in place + row sum; SIMD 16-wide chunks, scalar tail
        Simd(f32, 16) vmx = __simd_splat(mx)
        Simd(f32, 16) vsum = __simd_zero()
        int c = 0
        while c + 16 <= cols {
            Simd(f32, 16) v = __simd_load(p, base + c)
            v = smd.exp(v - vmx)
            __simd_store(p, base + c, v)
            vsum = vsum + v
            c = c + 16
        }
        f64 sum = __simd_reduce_add(vsum) as f64
        while c < cols {
            f64 e = math.exp((p[base + c] as f64) - (mx as f64))
            p[base + c] = e as f32
            sum = sum + e
            c = c + 1
        }
        // normalize by 1/sum
        f32 inv = (1.0 / sum) as f32
        Simd(f32, 16) vinv = __simd_splat(inv)
        c = 0
        while c + 16 <= cols {
            Simd(f32, 16) v = __simd_load(p, base + c)
            __simd_store(p, base + c, v * vinv)
            c = c + 16
        }
        while c < cols { p[base + c] = p[base + c] * inv; c = c + 1 }
        i = i + 1
    }
}

// Row-wise LayerNorm with affine (gamma, beta) over [rows, cols], in place. Each
// row is normalized to zero-mean / unit-variance (f64), then scaled and shifted.
def layernorm_rows(*f32 p, *f32 gamma, *f32 beta, int rows, int cols, f64 eps) {
    int i = 0
    while i < rows {
        int base = i * cols
        f64 mean = 0.0
        int j = 0
        while j < cols { mean = mean + (p[base + j] as f64); j = j + 1 }
        mean = mean / (cols as f64)
        f64 var = 0.0
        j = 0
        while j < cols { f64 d = (p[base + j] as f64) - mean; var = var + d * d; j = j + 1 }
        var = var / (cols as f64)
        f64 inv = 1.0 / math.sqrt(var + eps)
        j = 0
        while j < cols {
            f64 norm = ((p[base + j] as f64) - mean) * inv
            p[base + j] = (norm * (gamma[j] as f64) + (beta[j] as f64)) as f32
            j = j + 1
        }
        i = i + 1
    }
}

// GeLU (tanh approximation), in place over n contiguous f32. SIMD body + masked
// tail. gelu(x) = 0.5 * x * (1 + tanh( sqrt(2/pi) * (x + 0.044715 * x^3) )).
def gelu_inplace(*f32 p, int n) {
    Simd(f32, 16) c_sq = __simd_splat(0.7978845608028654)   // sqrt(2/pi)
    Simd(f32, 16) c_cu = __simd_splat(0.044715)
    Simd(f32, 16) half = __simd_splat(0.5)
    Simd(f32, 16) one  = __simd_splat(1.0)
    int i = 0
    while i + 16 <= n {
        Simd(f32, 16) x = __simd_load(p, i)
        Simd(f32, 16) inner = c_sq * (x + c_cu * x * x * x)
        Simd(f32, 16) t = smd.tanh(inner)
        __simd_store(p, i, half * x * (one + t))
        i = i + 16
    }
    int rem = n - i
    if rem > 0 {
        Simd(f32, 16) x = __simd_load_masked(p, i, rem)
        Simd(f32, 16) inner = c_sq * (x + c_cu * x * x * x)
        Simd(f32, 16) t = smd.tanh(inner)
        __simd_store_masked(p, i, half * x * (one + t), rem)
    }
}

// Transpose a row-major [rows, cols] matrix into dst, a [cols, rows] matrix.
def transpose(*f32 src, *f32 dst, int rows, int cols) {
    int i = 0
    while i < rows {
        int j = 0
        while j < cols { dst[j * rows + i] = src[i * cols + j]; j = j + 1 }
        i = i + 1
    }
}

// Elementwise a += b over n contiguous f32 (residual add). SIMD body + masked tail.
def add_inplace(*f32 a, *f32 b, int n) {
    int i = 0
    while i + 16 <= n {
        Simd(f32, 16) va = __simd_load(a, i)
        Simd(f32, 16) vb = __simd_load(b, i)
        __simd_store(a, i, va + vb)
        i = i + 16
    }
    int rem = n - i
    if rem > 0 {
        Simd(f32, 16) va = __simd_load_masked(a, i, rem)
        Simd(f32, 16) vb = __simd_load_masked(b, i, rem)
        __simd_store_masked(a, i, va + vb, rem)
    }
}

// Multi-head attention support. A head split takes a column block of the packed
// [seq, model_dim] Q/K/V projection into a contiguous [seq, head_dim] buffer;
// the merge scatters each head's context back. scale_inplace applies the
// 1/sqrt(head_dim) scaling before softmax (scaled dot-product attention).

// Gather column block [c0, c0+sub) of a row-major [rows, cols] matrix into a
// contiguous [rows, sub] matrix (attention head split).
def slice_cols(*f32 src, *f32 dst, int rows, int cols, int c0, int sub) {
    int i = 0
    while i < rows {
        int j = 0
        while j < sub { dst[i * sub + j] = src[i * cols + c0 + j]; j = j + 1 }
        i = i + 1
    }
}

// Scatter a contiguous [rows, sub] matrix back into column block [c0, c0+sub) of
// a row-major [rows, cols] matrix (attention head merge).
def place_cols(*f32 dst, *f32 src, int rows, int cols, int c0, int sub) {
    int i = 0
    while i < rows {
        int j = 0
        while j < sub { dst[i * cols + c0 + j] = src[i * sub + j]; j = j + 1 }
        i = i + 1
    }
}

// Slice column block [c0, c0+sub) of src[rows, cols] into dst TRANSPOSED as
// [sub, rows]: dst[j*rows + i] = src[i*cols + c0 + j]. Lets the per-head A·V run
// as chᵀ = vhᵀ·shᵀ with the wide T dimension (= rows) on the SIMD axis instead
// of the narrow head dim — see build_forward_transformer (C2, D1 AV-scalar fix).
def slice_cols_t(*f32 src, *f32 dst, int rows, int cols, int c0, int sub) {
    int i = 0
    while i < rows {
        int j = 0
        while j < sub { dst[j * rows + i] = src[i * cols + c0 + j]; j = j + 1 }
        i = i + 1
    }
}

// Scatter a TRANSPOSED [sub, rows] matrix into column block [c0, c0+sub) of a
// row-major [rows, cols] matrix: dst[i*cols + c0 + j] = srcT[j*rows + i]. Inverse
// of slice_cols_t for the merged attention context.
def place_cols_t(*f32 dst, *f32 srcT, int rows, int cols, int c0, int sub) {
    int i = 0
    while i < rows {
        int j = 0
        while j < sub { dst[i * cols + c0 + j] = srcT[j * rows + i]; j = j + 1 }
        i = i + 1
    }
}

// Multiply n contiguous f32 by scalar s, in place (attention 1/sqrt(d_k) scale).
// SIMD body + masked tail.
def scale_inplace(*f32 p, int n, f64 s) {
    Simd(f32, 16) vs = __simd_splat(s)
    int i = 0
    while i + 16 <= n {
        Simd(f32, 16) v = __simd_load(p, i)
        __simd_store(p, i, v * vs)
        i = i + 16
    }
    int rem = n - i
    if rem > 0 {
        Simd(f32, 16) v = __simd_load_masked(p, i, rem)
        __simd_store_masked(p, i, v * vs, rem)
    }
}

// ============================================================================
// Pool — a static f32 activation-tensor arena for hard-real-time inference.
//
// Fixed shapes ⇒ allocate the whole network's working memory ONCE, then carve
// activation buffers per forward pass with zero malloc on the hot path and a
// deterministic, abort-on-overflow capacity. Buffers are 64-byte (cache-line /
// AVX-512) aligned so a 16-wide load never straddles a cache line.
//
//     Pool p = {}
//     p.reserve(peak_floats)              // ONE malloc, up front
//     for each frame {
//         p.reset()                       // O(1) rewind — reuse the block
//         *f32 h1 = p.tensor(201 * 256)   // carve activations; no malloc
//         *f32 h2 = p.tensor(201 * 256)
//         ... run the kernels into h1/h2 ...
//     }
//     // p.~ frees the single backing block at scope exit
//
// PLANNING (sizing `reserve`): run one forward into an over-sized pool, then read
// `used_floats()` — that high-water mark is the exact `reserve` for production.
// Calling `reset()` at points where earlier activations are dead lowers the peak
// (manual liveness; a full overlap planner is future work). Raw pointers handed
// out before a reset() dangle afterwards — same contract as Region.
//
// `raw` is the malloc'd block (freed by ~); `base` is `raw` rounded up to 64.
struct Pool { *u8 raw; *u8 base; i64 off; i64 cap }

methods Pool {
    // Reserve the single backing block, sized for `max_floats` f32 values
    // (+ 64 bytes of slack so the base can be aligned to a cache line).
    def reserve(&!self, int max_floats) {
        i64 bytes = (max_floats as i64) * 4
        self.raw = c.malloc(bytes + 64) as *u8
        i64 addr = self.raw as i64
        i64 pad = (64 - (addr % 64)) % 64           // align base up to 64
        self.base = c.__ls_ptr_at(self.raw, pad)
        self.off = 0
        self.cap = bytes
    }

    // Carve `n` f32 (64-byte aligned); abort on overflow (fixed capacity).
    def tensor(&!self, int n) -> *f32 {
        i64 a = (self.off + 63) / 64 * 64           // align offset to 64
        i64 need = a + (n as i64) * 4
        if need > self.cap {
            @print(f"nn.Pool out of capacity: cap={self.cap} need={need}")
            c.abort()
        }
        *u8 p = c.__ls_ptr_at(self.base, a)
        self.off = need
        return p as *f32
    }

    // Carve `n` f32 at a CALLER-CHOSEN offset (in f32 units) instead of bumping.
    // `off_floats` must be 16-aligned (=64 bytes; multiple buffers at the same
    // offset is how a liveness planner reuses dead slots). Bounds-checked against
    // cap; tracks the high-water mark so used_floats() still reports peak usage.
    def at(&!self, int off_floats, int n) -> *f32 {
        i64 a = (off_floats as i64) * 4
        i64 need = a + (n as i64) * 4
        if need > self.cap {
            @print(f"nn.Pool out of capacity: cap={self.cap} need={need}")
            c.abort()
        }
        *u8 p = c.__ls_ptr_at(self.base, a)
        if need > self.off { self.off = need }
        return p as *f32
    }

    // O(1) rewind: reclaim all carved tensors, keep the block for the next frame.
    def reset(&!self) { self.off = 0 }

    // High-water mark in f32 units — read after a dry-run forward to size reserve().
    def used_floats(&self) -> int { return (self.off / 4) as int }
    def cap_floats(&self) -> int { return (self.cap / 4) as int }
}

methods Pool: Destroy {
    // Free the single backing block (activation buffers are raw f32 → no per-
    // tensor drop).
    def ~(&!self) {
        if self.cap > 0 { c.free(self.raw) }
    }
}
