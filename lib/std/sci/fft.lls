// std/fft.ls — Fast Fourier Transform over Complex(f64).
//
// Phase 2: radix-2 Cooley-Tukey (decimation-in-time) for N = 2^k.
// Phase 3: Bluestein (chirp-z) for ARBITRARY N (incl. large primes) — any DFT of
//          length N is turned into a convolution evaluated by a power-of-2 FFT.
// (Mixed-radix 3/4/5 kernels are a perf optimization for composite N; Bluestein
//  already handles every N correctly, so they are deferred. Multi-dim is Phase 5.)
//
// The whole module is NON-generic (every value is Complex(f64)) — FFT is only
// meaningful for floats, so there is no type parameter and helpers call each other
// freely. Call qualified: `import std.sci.fft as ft` then `ft.fft(...)`.
//
//   fft(Vec(Complex(f64)))  -> Vec(Complex(f64))   forward, unscaled
//   ifft(Vec(Complex(f64))) -> Vec(Complex(f64))   inverse, scaled by 1/N (NumPy)
//
// Convention: forward W_N^k = exp(-2*pi*i*k/N); inverse +sign and 1/N.

import std.core.vec
import std.sci.complex
import std.sci.tensor
import std.core.math as math

// n is a power of two (n >= 1)
def _is_pow2(int n) -> bool { return n > 0 && (n & (n - 1)) == 0 }

// smallest power of two >= n (n >= 1)
def _next_pow2(int n) -> int {
    int p = 1
    while p < n { p = p * 2 }
    return p
}

// In-place bit-reversal permutation of `a` (length n = 2^k).
def _bitrev(&!Vec(Complex(f64)) a, int n) {
    int j = 0
    int i = 1
    while i < n {
        int bit = n >> 1
        while (j & bit) != 0 {
            j = j ^ bit            // clear the set high bit
            bit = bit >> 1
        }
        j = j | bit
        if i < j {
            Complex(f64) tmp = a.get!(i)
            a.set!(i, a.get!(j))
            a.set!(j, tmp)
        }
        i = i + 1
    }
}

// Iterative DIT butterflies over a bit-reversed `a`. `sign` = -1 forward, +1 inverse.
def _butterfly(&!Vec(Complex(f64)) a, int n, f64 sign) {
    int len = 2
    while len <= n {
        f64 ang = sign * 2.0 * math.PI / (len as f64)
        Complex(f64) wlen = c(f64)(math.cos(ang), math.sin(ang))
        int half = len / 2
        int base = 0
        while base < n {
            Complex(f64) w = c(f64)(1.0, 0.0)
            int j = 0
            while j < half {
                Complex(f64) u = a.get!(base + j)
                Complex(f64) t = a.get!(base + j + half) * w
                a.set!(base + j, u + t)
                a.set!(base + j + half, u - t)
                w = w * wlen
                j = j + 1
            }
            base = base + len
        }
        len = len * 2
    }
}

// Bluestein chirp factor exp(sign * i*pi*n^2/N). n^2 reduced mod 2N (the period of
// exp(-i*pi*n^2/N)) in i64 to avoid overflow and keep precision for large N.
def _chirp(int n, int bigN, f64 sign) -> Complex(f64) {
    i64 nn = (n as i64) * (n as i64)
    i64 period = 2 * (bigN as i64)
    i64 m = nn % period
    f64 ang = sign * math.PI * (m as f64) / (bigN as f64)
    return c(f64)(math.cos(ang), math.sin(ang))
}

// Bluestein forward DFT for arbitrary N (used by fft when N is not a power of 2).
//   X_k = chirp_neg[k] * (a (*) b)_k,  a_n = x_n*chirp_neg[n],  b = symmetric chirp_pos
// where the circular convolution (length M = next_pow2(2N-1)) is done with radix-2.
def _bluestein(Vec(Complex(f64)) x) -> Vec(Complex(f64)) {
    int bigN = x.len()
    int m = _next_pow2(2 * bigN - 1)

    // a[n] = x[n] * exp(-i*pi*n^2/N), zero-padded to length m
    Vec(Complex(f64)) a = []
    int n = 0
    while n < m {
        if n < bigN {
            a.push(x.get!(n) * _chirp(n, bigN, 0.0 - 1.0))
        } else {
            a.push(c(f64)(0.0, 0.0))
        }
        n = n + 1
    }

    // b: filter with b[k] = b[m-k] = exp(+i*pi*k^2/N) for k in [0,N), else 0
    Vec(Complex(f64)) b = []
    int z = 0
    while z < m { b.push(c(f64)(0.0, 0.0)); z = z + 1 }
    int k = 0
    while k < bigN {
        Complex(f64) bp = _chirp(k, bigN, 1.0)
        b.set!(k, bp)
        if k > 0 { b.set!(m - k, bp) }
        k = k + 1
    }

    // circular convolution via radix-2: conv = ifft(fft(a) .* fft(b))
    _bitrev(&!a, m)
    _butterfly(&!a, m, 0.0 - 1.0)
    _bitrev(&!b, m)
    _butterfly(&!b, m, 0.0 - 1.0)
    Vec(Complex(f64)) cc = []
    int j = 0
    while j < m { cc.push(a.get!(j) * b.get!(j)); j = j + 1 }
    _bitrev(&!cc, m)
    _butterfly(&!cc, m, 1.0)             // inverse butterfly (scale 1/m below)

    f64 invm = 1.0 / (m as f64)
    Vec(Complex(f64)) out = []
    int p = 0
    while p < bigN {
        Complex(f64) ck = cc.get!(p).scale(invm)
        out.push(ck * _chirp(p, bigN, 0.0 - 1.0))
        p = p + 1
    }
    return out
}

// smallest prime factor of n (n >= 2); returns n itself if n is prime
def _smallest_factor(int n) -> int {
    int f = 2
    while f * f <= n {
        if n % f == 0 { return f }
        f = f + 1
    }
    return n
}

// n is "smooth" — all prime factors <= 7, so recursive mixed-radix is efficient
def _is_smooth(int n) -> bool {
    int m = n
    while m % 2 == 0 { m = m / 2 }
    while m % 3 == 0 { m = m / 3 }
    while m % 5 == 0 { m = m / 5 }
    while m % 7 == 0 { m = m / 7 }
    return m == 1
}

// Recursive mixed-radix Cooley-Tukey (decimation-in-time), unscaled. At each level
// it splits N = p*q on the smallest prime factor p: FFT the p length-q subsequences
// x_r[m] = x[p*m+r], then combine X[k] = sum_r W_N^{sign*r*k} * Xr[k mod q]. O(N log N)
// for smooth N; degrades to O(N^2) at a large prime factor (callers route those to
// Bluestein instead). sign = -1 forward, +1 inverse.
def _fft_mixed(Vec(Complex(f64)) x, f64 sign) -> Vec(Complex(f64)) {
    int n = x.len()
    if n <= 1 { return x }
    if _is_pow2(n) {
        _bitrev(&!x, n)
        _butterfly(&!x, n, sign)
        return x
    }
    int p = _smallest_factor(n)
    int q = n / p
    // FFT each of the p subsequences; pack results flat: sub[r*q + j]
    Vec(Complex(f64)) sub = []
    int r = 0
    while r < p {
        Vec(Complex(f64)) xr = []
        int m = 0
        while m < q { xr.push(x.get!(p * m + r)); m = m + 1 }
        Vec(Complex(f64)) xrf = _fft_mixed(xr, sign)
        int j = 0
        while j < q { sub.push(xrf.get!(j)); j = j + 1 }
        r = r + 1
    }
    // combine
    Vec(Complex(f64)) out = []
    int z = 0
    while z < n { out.push(c(f64)(0.0, 0.0)); z = z + 1 }
    int k = 0
    while k < n {
        int kq = k % q
        Complex(f64) acc = c(f64)(0.0, 0.0)
        int rr = 0
        while rr < p {
            f64 ang = sign * 2.0 * math.PI * (rr as f64) * (k as f64) / (n as f64)
            Complex(f64) w = c(f64)(math.cos(ang), math.sin(ang))
            acc = acc + w * sub.get!(rr * q + kq)
            rr = rr + 1
        }
        out.set!(k, acc)
        k = k + 1
    }
    return out
}

// Forward FFT (unscaled). Power-of-2 -> radix-2; other smooth N -> mixed-radix;
// N with a large prime factor -> Bluestein.
def fft(Vec(Complex(f64)) x) -> Vec(Complex(f64)) {
    int n = x.len()
    if n <= 1 { return x }
    if _is_pow2(n) {
        _bitrev(&!x, n)
        _butterfly(&!x, n, 0.0 - 1.0)
        return x
    }
    if _is_smooth(n) { return _fft_mixed(x, 0.0 - 1.0) }
    return _bluestein(x)
}

// Inverse FFT (1/N scaling, NumPy). Works for ANY N via the conjugate identity
// ifft(X) = conj(fft(conj(X))) / N, reusing the arbitrary-N forward transform.
def ifft(Vec(Complex(f64)) x) -> Vec(Complex(f64)) {
    int n = x.len()
    if n <= 1 { return x }
    int i = 0
    while i < n { x.set!(i, x.get!(i).conj()); i = i + 1 }
    Vec(Complex(f64)) y = fft(x)
    f64 inv = 1.0 / (n as f64)
    int j = 0
    while j < n { y.set!(j, y.get!(j).conj().scale(inv)); j = j + 1 }
    return y
}

// ---- real-input transforms (rfft / irfft) ----
//
// A real signal's spectrum is Hermitian (X[N-k] = conj(X[k])), so only the first
// N/2+1 bins are independent. rfft returns just those (NumPy np.fft.rfft).
//
// For even N, the ~2x packed-real trick is used: pack the N reals into N/2 complex
// (even index -> re, odd -> im), run a HALF-length (N/2) FFT, then unpack via the
// conjugate-symmetric even/odd split. For odd N (where packing doesn't apply) it
// falls back to a full complex FFT + truncate.

def rfft(Vec(f64) x) -> Vec(Complex(f64)) {
    int n = x.len()
    Vec(Complex(f64)) out = []
    if n == 0 { return out }
    if n % 2 == 1 {
        // odd N: full complex FFT + truncate to N/2+1
        Vec(Complex(f64)) cx = []
        int i = 0
        while i < n { cx.push(c(f64)(x.get!(i), 0.0)); i = i + 1 }
        Vec(Complex(f64)) full = fft(cx)
        int half = n / 2 + 1
        int k = 0
        while k < half { out.push(full.get!(k)); k = k + 1 }
        return out
    }
    // even N: packed half-length FFT
    int m = n / 2
    Vec(Complex(f64)) z = []
    int j = 0
    while j < m { z.push(c(f64)(x.get!(2 * j), x.get!(2 * j + 1))); j = j + 1 }
    Vec(Complex(f64)) zf = fft(z)                 // length m
    Complex(f64) neg_i_half = c(f64)(0.0, 0.0 - 0.5)
    int k = 0
    while k <= m {
        int kk = k % m                            // Z periodic: Z[m] = Z[0]
        int mk = (m - k) % m
        Complex(f64) zk = zf.get!(kk)
        Complex(f64) zc = zf.get!(mk).conj()
        Complex(f64) xe = (zk + zc).scale(0.5)    // even-sample DFT
        Complex(f64) xo = (zk - zc) * neg_i_half  // odd-sample DFT / (2i)
        f64 ang = 0.0 - 2.0 * math.PI * (k as f64) / (n as f64)
        Complex(f64) w = c(f64)(math.cos(ang), math.sin(ang))
        out.push(xe + w * xo)
        k = k + 1
    }
    return out
}

// irfft(half-spectrum X of length N/2+1, original length n) -> real Vec(f64)
def irfft(Vec(Complex(f64)) x, int n) -> Vec(f64) {
    int hl = x.len()                          // n/2 + 1
    Vec(Complex(f64)) full = []
    int z = 0
    while z < n { full.push(c(f64)(0.0, 0.0)); z = z + 1 }
    int k = 0
    while k < hl { full.set!(k, x.get!(k)); k = k + 1 }
    // mirror the conjugate-symmetric upper half: full[k] = conj(x[n-k])
    k = hl
    while k < n { full.set!(k, x.get!(n - k).conj()); k = k + 1 }
    Vec(Complex(f64)) t = ifft(full)
    Vec(f64) out = []
    int p = 0
    while p < n { out.push(t.get!(p).re); p = p + 1 }
    return out
}

// ---- DCT (discrete cosine transform), scipy norm=None convention ----
//
// Makhoul's method: O(N log N) via one length-N FFT (was O(N^2) direct). DCT-II
// reorders the input (evens ascending, odds descending), FFTs, then applies a
// half-sample twiddle and takes the real part. idct is the exact inverse (so
// idct(dct(x)) == x), reconstructed from the conjugate-symmetric structure.
//
//   dct (DCT-II):   y_k = 2 * sum_n x_n cos(pi*(2n+1)*k / (2N))

// even-odd reorder index used by both dct and idct:
//   i < (N+1)/2 -> 2i      (even samples, ascending)
//   else        -> 2N-1-2i (odd samples, descending)
def _dct_reorder_src(int i, int bigN) -> int {
    if i < (bigN + 1) / 2 { return 2 * i }
    return 2 * bigN - 1 - 2 * i
}

def dct(Vec(f64) x) -> Vec(f64) {
    int bigN = x.len()
    Vec(Complex(f64)) v = []
    int i = 0
    while i < bigN {
        v.push(c(f64)(x.get!(_dct_reorder_src(i, bigN)), 0.0))
        i = i + 1
    }
    Vec(Complex(f64)) cap_v = fft(v)
    f64 d2n = 2.0 * (bigN as f64)
    Vec(f64) y = []
    int k = 0
    while k < bigN {
        f64 ang = 0.0 - math.PI * (k as f64) / d2n     // exp(-i*pi*k/(2N))
        Complex(f64) w = c(f64)(math.cos(ang), math.sin(ang))
        Complex(f64) a = w * cap_v.get!(k)
        y.push(2.0 * a.re)
        k = k + 1
    }
    return y
}

// inverse DCT-II (exact inverse of dct): reconstruct the FFT spectrum from y,
// ifft, then undo the even-odd reorder. A[k] = (y[k] - i*y[N-k]) / 2 (y[N]=0).
def idct(Vec(f64) y) -> Vec(f64) {
    int bigN = y.len()
    f64 d2n = 2.0 * (bigN as f64)
    Vec(Complex(f64)) cap_v = []
    int k = 0
    while k < bigN {
        f64 ynk = 0.0
        if k > 0 { ynk = y.get!(bigN - k) }
        Complex(f64) a = c(f64)(0.5 * y.get!(k), 0.0 - 0.5 * ynk)
        f64 ang = math.PI * (k as f64) / d2n           // exp(+i*pi*k/(2N))
        Complex(f64) w = c(f64)(math.cos(ang), math.sin(ang))
        cap_v.push(w * a)
        k = k + 1
    }
    Vec(Complex(f64)) v = ifft(cap_v)
    Vec(f64) x = []
    int z = 0
    while z < bigN { x.push(0.0); z = z + 1 }
    int i = 0
    while i < bigN {
        x.set!(_dct_reorder_src(i, bigN), v.get!(i).re)
        i = i + 1
    }
    return x
}

// ---- multi-dimensional FFT (multi-pass / separable) ----
//
// fftn transforms a Tensor(Complex(f64)) along every axis: for each axis, gather
// each line (the strided run along that axis) into a contiguous Vec, run the 1D
// fft, scatter back. One pass per dimension. Reuses the arbitrary-N 1D kernel, so
// each axis length can be any N. fft2 is the rank-2 convenience name.
//
// Line enumeration relies on contiguous row-major strides (strides[a] = product of
// shape[a+1..]) — true for any owned Tensor. For axis a with stride sa and length
// na, the line bases are outer*(na*sa) + inner for outer in [0,size/(na*sa)) and
// inner in [0,sa).

def _fft_1d(Vec(Complex(f64)) line, bool inverse) -> Vec(Complex(f64)) {
    if inverse { return ifft(line) }
    return fft(line)
}

// transform every line along `axis` of `t` in place (1D fft or ifft)
def _fft_axis(&!Tensor(Complex(f64)) t, int axis, bool inverse) {
    Vec(int) strd = t.strides()
    int na = t.dim(axis)
    int sa = strd.get!(axis)
    int total = t.size()
    int block = na * sa
    int num_outer = total / block
    int outer = 0
    while outer < num_outer {
        int inner = 0
        while inner < sa {
            int base = outer * block + inner
            Vec(Complex(f64)) line = []
            int j = 0
            while j < na { line.push(t.get!(base + j * sa)); j = j + 1 }
            Vec(Complex(f64)) tr = _fft_1d(line, inverse)
            int q = 0
            while q < na { t.set!(base + q * sa, tr.get!(q)); q = q + 1 }
            inner = inner + 1
        }
        outer = outer + 1
    }
}

// N-dimensional forward FFT (all axes). Input cloned by value; result is new.
def fftn(Tensor(Complex(f64)) t) -> Tensor(Complex(f64)) {
    int r = t.rank()
    int a = 0
    while a < r { _fft_axis(&!t, a, false); a = a + 1 }
    return t
}

// N-dimensional inverse FFT (1/N per axis = 1/total overall, NumPy).
def ifftn(Tensor(Complex(f64)) t) -> Tensor(Complex(f64)) {
    int r = t.rank()
    int a = 0
    while a < r { _fft_axis(&!t, a, true); a = a + 1 }
    return t
}

// rank-2 convenience names
def fft2(Tensor(Complex(f64)) t) -> Tensor(Complex(f64)) { return fftn(t) }
def ifft2(Tensor(Complex(f64)) t) -> Tensor(Complex(f64)) { return ifftn(t) }
