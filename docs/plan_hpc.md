# LS High-Performance Computing — Implementation Plan

## Overview

This plan covers the infrastructure needed to make LS viable for numerical computing:
SIMD intrinsics, complex numbers, FFT/DCT, linear algebra, and parallelism hooks.

The overall philosophy is the same as the rest of LS: **zero-cost abstractions via LLVM**.
All numeric primitives lower to LLVM vector instructions or well-known LLVM intrinsics,
with no runtime overhead beyond what a hand-written C program would have.

---

## Phase HPC.1 — SIMD Vector Types & Intrinsics

### Goal

Expose explicit SIMD types so performance-critical code can guarantee vectorisation
without relying on the LLVM auto-vectoriser.

### New types

```ls
// Fixed-width SIMD types (all value types, no heap allocation)
v4f32    // 4 × f32  (128-bit SSE/NEON)
v8f32    // 8 × f32  (256-bit AVX)
v4f64    // 4 × f64  (256-bit AVX)
v16i8    // 16 × i8  (128-bit)
v8i16    // 8 × i16  (128-bit)
v4i32    // 4 × i32  (128-bit)
v8i32    // 8 × i32  (256-bit AVX2)
v4i64    // 4 × i64  (256-bit AVX2)
```

### Built-in `simd` module

```ls
import simd

// --- Load / store ---
v4f32 a = simd.load_f32x4([1.0, 2.0, 3.0, 4.0])     // from array literal
v4f32 a = simd.load_ptr_f32x4(ptr)                    // from *f32 pointer

simd.store_ptr_f32x4(dst_ptr, a)

// --- Arithmetic ---
v4f32 c = simd.add_f32x4(a, b)
v4f32 c = simd.sub_f32x4(a, b)
v4f32 c = simd.mul_f32x4(a, b)
v4f32 c = simd.div_f32x4(a, b)
v4f32 c = simd.fma_f32x4(a, b, c)   // a*b + c  (fused multiply-add)
v4f32 c = simd.sqrt_f32x4(a)
v4f32 c = simd.abs_f32x4(a)
v4f32 c = simd.min_f32x4(a, b)
v4f32 c = simd.max_f32x4(a, b)

// --- Reduction ---
f32 s = simd.hadd_f32x4(a)          // a[0]+a[1]+a[2]+a[3]
f32 m = simd.hmax_f32x4(a)

// --- Shuffle / permute ---
v4f32 c = simd.shuffle_f32x4(a, b, [0, 2, 4, 6])   // cross-lane shuffle
v4f32 c = simd.broadcast_f32x4(1.0)                  // splat scalar

// --- Comparison (returns mask) ---
v4i32 mask = simd.cmp_lt_f32x4(a, b)
v4f32 c    = simd.blend_f32x4(a, b, mask)   // select(mask, b, a)

// --- Conversion ---
v4i32 c = simd.cvt_f32x4_to_i32x4(a)       // truncate
v4f32 c = simd.cvt_i32x4_to_f32x4(a)
```

### Codegen implementation

Each `simd.*` call emits the corresponding LLVM vector instruction directly:
- `simd.add_f32x4` → `fadd <4 x float>`
- `simd.fma_f32x4` → `call <4 x float> @llvm.fma.v4f32(...)`
- `simd.sqrt_f32x4` → `call <4 x float> @llvm.sqrt.v4f32(...)`
- `simd.shuffle_f32x4` → `shufflevector`

No function call overhead; all expanded inline.

### `@vectorize` loop hint

```ls
// Hint to LLVM auto-vectoriser: unroll by 4 and emit vector instructions
@vectorize(4)
for i in 0..n {
    out[i] = a[i] * b[i] + c[i]
}
```

Implemented by emitting LLVM loop metadata (`llvm.loop.vectorize.width`).

**Effort**: 8–12 days

---

## Phase HPC.2 — Complex Numbers

### New type `complex`

```ls
complex z1 = complex(3.0, 4.0)       // 3 + 4i
complex z2 = 2.0 + 1.5i              // literal syntax (scanner: suffix 'i' on float)
f64 r  = z1.re                        // 3.0
f64 im = z1.im                        // 4.0
f64 m  = z1.abs()                     // |z| = 5.0
f64 a  = z1.arg()                     // atan2(im, re)
complex c = z1 + z2
complex c = z1 * z2
complex c = z1.conj()                 // conjugate
complex c = z1.inv()                  // 1/z
complex c = math.exp(z)               // e^z
complex c = math.sqrt(z)              // principal square root
complex c = math.log(z)               // principal log
```

### Implementation

`complex` is a **struct with two f64 fields** — no heap allocation.  
Layout: `{f64 re, f64 im}` (C-ABI compatible with `_Complex double`).

Arithmetic operators `+` `-` `*` `/` overloaded in the checker for `TYPE_COMPLEX`.
`math` module extended: `math.exp(complex)`, `math.log(complex)`, `math.sqrt(complex)`.

**Effort**: 4–6 days

---

## Phase HPC.3 — FFT & DCT

### API

```ls
import fft

// --- FFT (Cooley-Tukey, power-of-2 input length) ---
vec(complex) X = fft.fft(x)         // forward DFT
vec(complex) x = fft.ifft(X)        // inverse DFT (normalised by 1/N)

// Real FFT (input is real, output is N/2+1 complex — saves ~half the work)
vec(complex) X = fft.rfft(x)        // x: vec(f64), returns N/2+1 complex
vec(f64)     x = fft.irfft(X, n)    // n: original signal length

// --- Power spectrum ---
vec(f64) ps = fft.power_spectrum(x)    // |X[k]|^2 / N^2
vec(f64) freq = fft.freqs(n, sample_rate_hz)  // frequency axis in Hz

// --- DCT (Type II: used in JPEG/MP3/AAC) ---
vec(f64) c = fft.dct2(x)    // forward DCT-II
vec(f64) x = fft.idct2(c)   // inverse (DCT-III / IDCT)

// --- Short-time Fourier transform (STFT) ---
// Returns vec of frequency frames: vec(vec(complex))
vec(vec(complex)) S = fft.stft(x, window_size: 1024, hop: 256)
```

### Implementation strategy

**Phase HPC.3a — Pure LS Cooley-Tukey FFT**:
- Iterative (bit-reversal permutation + butterfly) — no recursion.
- Twiddle factors precomputed and cached per transform size.
- Uses `vec(complex)` with existing memory management.
- Target: competitive with naive C implementation (not FFTW).

**Phase HPC.3b — FFTW FFI wrapper (optional, opt-in)**:
- `extern` wrapper around `fftw3` for production-grade speed.
- `fft.use_backend(fft.FFTW)` at program start if fftw3.so is available.
- Falls back to pure-LS if not available.

**Effort**: HPC.3a: 7–10 days · HPC.3b: 3–5 days additional

---

## Phase HPC.4 — Linear Algebra (`linalg` module)

### Types

```ls
// Heap-allocated 2D matrix, row-major order
linalg.Matrix A = linalg.matrix(rows: 4, cols: 4)      // zero-initialised
linalg.Matrix A = linalg.eye(4)                          // 4×4 identity
linalg.Matrix A = linalg.from_array([[1,2],[3,4]])

int   rows = A.rows
int   cols = A.cols
f64   v    = A[i, j]         // element access (bounds-checked)
A[i, j]  = 3.14              // element write

// --- Basic operations ---
linalg.Matrix C = linalg.add(A, B)
linalg.Matrix C = linalg.sub(A, B)
linalg.Matrix C = linalg.scale(A, 2.0)
linalg.Matrix C = linalg.mul(A, B)         // matrix multiply (GEMM)
linalg.Matrix T = linalg.transpose(A)
f64            d = linalg.det(A)
linalg.Matrix I = linalg.inv(A)            // LU-based inverse

// --- Decompositions ---
linalg.LU   lu  = linalg.lu(A)
linalg.QR   qr  = linalg.qr(A)
linalg.SVD  svd = linalg.svd(A)
linalg.Eig  e   = linalg.eig(A)           // eigenvalues + eigenvectors (symmetric)

// --- Linear systems ---
vec(f64) x = linalg.solve(A, b)    // solve Ax = b
vec(f64) x = linalg.lstsq(A, b)    // least squares

// --- Vector ops ---
f64       d = linalg.dot(u, v)
f64       n = linalg.norm(v)          // L2 norm
vec(f64) nv = linalg.normalize(v)
vec(f64) cv = linalg.cross(u, v)    // 3D cross product
```

### Implementation strategy

**Phase HPC.4a — Pure LS (GEMM via SIMD)**:
- Matrix stored as `vec(f64)` (row-major).
- GEMM uses tiled multiplication with SIMD `fma_f64x4` for AVX platforms.
- LU decomposition, QR (Householder), basic SVD (bidiagonalisation + Golub-Reinsch).

**Phase HPC.4b — OpenBLAS / MKL FFI (optional, opt-in)**:
- `linalg.use_backend(linalg.OPENBLAS)` at program start.
- `dgemm`, `dgetrf`, `dgesvd` etc. via `extern` declarations.
- 10–50× faster than pure LS for large matrices.

**Effort**: HPC.4a: 14–21 days · HPC.4b: 5–7 days additional

---

## Phase HPC.5 — Parallel Loops (`@parallel`)

### Goal

Let the user annotate compute-intensive loops for automatic parallelisation.

```ls
@parallel
for i in 0..n {
    out[i] = heavy_compute(data[i])
}

// With explicit thread count:
@parallel(threads: 8)
for i in 0..n {
    out[i] = a[i] * b[i]
}
```

### Implementation options

**Option A — OpenMP codegen**: Emit LLVM OpenMP runtime calls.
The LLVM OMP runtime (`libomp`) is available on both Linux (`libgomp` or `libomp`) and Windows.
This is the same approach Julia uses for `@threads`.

**Option B — Thread pool** (custom): A lightweight work-stealing thread pool
written in LS using `process.thread` primitives (after the concurrency plan).
More portable, harder to get right.

**Recommended**: Option A for an initial implementation.

**Effort**: 7–10 days

---

## Phase Summary

| Phase | Feature | Prerequisite | Effort |
|-------|---------|--------------|--------|
| HPC.1 | SIMD types + `simd` module | none | 8–12 d |
| HPC.2 | `complex` type | none | 4–6 d |
| HPC.3a | Pure-LS FFT / DCT | HPC.2 | 7–10 d |
| HPC.3b | FFTW FFI wrapper | HPC.3a, Phase S FFI maturity | 3–5 d |
| HPC.4a | Pure-LS linalg (GEMM + decompositions) | HPC.1, HPC.2 | 14–21 d |
| HPC.4b | OpenBLAS/MKL FFI backend | HPC.4a | 5–7 d |
| HPC.5 | `@parallel` loops | none (Option A) | 7–10 d |

**Recommended start**: HPC.2 (complex) → HPC.3a (FFT) → HPC.1 (SIMD) → HPC.4a.
Complex numbers and FFT deliver the most value for the least effort.
