// std/tensor.ls — N-dimensional heap tensor (NumPy-like), pure LS.
//
// Tensor(T): a heap-backed, runtime-shaped, row-major contiguous N-D array of
// POD numeric T (int/i64/f32/f64/...). Storage is a single malloc'd *T buffer of
// `size` elements; shape/strides are runtime Vec(int) (rank = shape.len()), so
// the same type covers 1-D..N-D with NO compile-time dimensions. This is the AI /
// NumPy workhorse — see docs/plan_ndarray_stdlib.md §-1.
//
// Construction (the `{}` + init idiom — generic free-fn constructors are not yet
// cross-module instantiable, so init is a generic METHOD like Vec.push):
//     Tensor(f64) t = {}            // empty (data=nil, size=0)
//     t.init_zeros(sh)             // allocate sh-shaped zeros   (sh: Vec(int))
//     t.init(sh, 1.5)              // allocate sh-shaped, filled with 1.5
//     t.init_from(sh, flat)        // allocate from a flat row-major Vec(T)
//
// Semantics (like Vec, NOT a value array): Tensor is a has_drop / MOVE type.
//   * `b = a`        moves a (a dead after).   `a.copy()`  explicit deep copy.
//   * by-value param clones (matches all user has_drop structs).
//   * `__drop` frees the data buffer; the shape/strides Vec fields auto-drop.
//
// Three-layer flat access (tensor as a flat buffer of `size` elements, storage
// order = row-major):
//   * get(f)/set(f)    bounds-checked; out-of-range aborts (the safe default).
//   * get!(f)/set!(f)  UNCHECKED raw load/store (the `!` = unsafe escape hatch).
// Multi-index at2/set2/at3/set3 are bounds-checked; `t[i,j,k]` sugar is phase 2.
//
// C interop: as_ptr()/as_mut_ptr() -> *T give the contiguous buffer base for
// extern fn / BLAS. The pointer is valid only while the tensor is alive (no
// lifetime system — same red line as Vec.as_ptr / map by-ref capture).
//
// NOTE: runtime primitives are reached via the canonical std.c.malloc/free/abort
// path (literal-matched, no `import std.c as c` alias — that alias is invisible
// when a generic method is instantiated in the consumer module, same as std.vec).

import std.vec
import std.num
import math

struct Tensor(T) {
    *T data            // heap buffer of `size` T, row-major contiguous
    Vec(int) shape     // dim lengths; rank = shape.len()
    Vec(int) strides   // row-major element strides: strides[k] = prod(shape[k+1..])
    int size           // prod(shape)
}

// Broadcast plan (POD, non-generic): result shape + per-operand broadcast strides
// (0 on stretched axes) + result element count. Produced by Tensor._bcast_plan.
struct BCast { Vec(int) shape; Vec(int) sa; Vec(int) sb; int size }

// ---- one-line generic constructors (free fns) ----
//
// These are the ergonomic counterpart to the `{}` + init_* methods: a single
// expression `zeros(f64)(sh)`. They are GENERIC FREE FUNCTIONS — `import std.tensor`
// registers them into the caller's fn-template registry, so they are called by
// BARE name with an explicit element type: `zeros(f64)(sh)`, NOT `tensor.zeros(...)`
// (generic free fns are not module-qualified; that is how LS cross-module generic
// fns work — see the import path in src/checker.c). Each is self-contained (it only
// calls the cross-module-safe _setup method + std.c.malloc), so it instantiates
// cleanly in any importer.

// zeros(T)(sh): a sh-shaped tensor of 0.
fn zeros(T)(Vec(int) shape) -> Tensor(T) {
    Tensor(T) out = {}
    int n = out._setup(shape);
    *T p = std.c.malloc(n * sizeof(T)) as *T
    int i = 0
    while i < n { p[i] = T.zero(); i = i + 1 }
    out.data = p
    return out
}

// full(T)(sh, v): a sh-shaped tensor with every element = v.
fn full(T)(Vec(int) shape, T fill) -> Tensor(T) {
    Tensor(T) out = {}
    int n = out._setup(shape);
    *T p = std.c.malloc(n * sizeof(T)) as *T
    int i = 0
    while i < n { p[i] = fill; i = i + 1 }
    out.data = p
    return out
}

// from_vec(T)(sh, data): a sh-shaped tensor from a flat row-major Vec(T). `src`
// is taken by value (cloned once) — a by-value Vec param avoids a method-on-&Vec
// resolution gap in generic-free-fn instantiation, at the cost of one copy.
fn from_vec(T)(Vec(int) shape, Vec(T) src) -> Tensor(T) {
    Tensor(T) out = {}
    int n = out._setup(shape);
    if src.len() != n {
        print(f"Tensor from_vec size mismatch: data len={src.len()} shape prod={n}")
        std.c.abort()
    }
    *T p = std.c.malloc(n * sizeof(T)) as *T
    int i = 0
    while i < n { p[i] = src.get!(i); i = i + 1 }
    out.data = p
    return out
}

impl(T) Tensor(T) {
    // ---- internal: set shape/strides/size from `shape`; return element count n.
    // Does NOT allocate data — the caller allocates data[n] next. Call on a fresh
    // `{}` tensor (or after the old buffer has been handled).
    fn _setup(&!self, &Vec(int) shape) -> int {
        int r = shape.len()
        int n = 1
        int k = 0
        while k < r { n = n * shape.get!(k); k = k + 1 }
        // row-major strides: strides[k] = prod(shape[k+1..])
        Vec(int) st = {}
        int z = 0
        while z < r { st.push(0); z = z + 1 }
        int acc = 1
        int q = r - 1
        while q >= 0 {
            st.set(q, acc)
            acc = acc * shape.get!(q)
            q = q - 1
        }
        self.shape = shape.copy()
        self.strides = st
        self.size = n
        return n
    }

    // ---- construction (call on `Tensor(T) t = {}`) ----

    // Allocate a shape-shaped buffer, every element = fill.
    fn init(&!self, &Vec(int) shape, T fill) {
        int n = self._setup(shape);   // ';' guards the line-leading '*' below
        *T p = std.c.malloc(n * sizeof(T)) as *T
        int i = 0
        while i < n { p[i] = fill; i = i + 1 }
        self.data = p
    }
    // Allocate shape-shaped zeros (T.zero() per element).
    fn init_zeros(&!self, &Vec(int) shape) { self.init(shape, T.zero()) }

    // Allocate from a flat row-major Vec(T); src.len() must equal prod(shape).
    fn init_from(&!self, &Vec(int) shape, &Vec(T) src) {
        int n = self._setup(shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        if src.len() != n {
            print(f"Tensor.init_from size mismatch: data len={src.len()} shape prod={n}")
            std.c.abort()
        }
        int i = 0
        while i < n { p[i] = src.get!(i); i = i + 1 }
        self.data = p
    }

    // ---- queries ----
    fn rank(&self) -> int { return self.shape.len() }
    fn size(&self) -> int { return self.size }
    fn dim(&self, int k) -> int { return self.shape.get!(k) }
    fn shape(&self) -> Vec(int) { return self.shape.copy() }
    fn strides(&self) -> Vec(int) { return self.strides.copy() }
    fn as_ptr(&self) -> *T { return self.data }
    fn as_mut_ptr(&!self) -> *T { return self.data }

    // ---- flat (linear) access: tensor as a flat buffer of `size` elements ----

    // UNCHECKED raw read/store of flat slot f. Caller MUST keep f in [0, size).
    fn get!(&self, int f) -> T { return self.data[f] }
    fn set!(&!self, int f, T v) { self.data[f] = v }

    // Safe recoverable read: deep clone of flat slot f, or None when out-of-range.
    fn get(&self, int f) -> Option(T) {
        if f < 0 || f >= self.size { return None }
        return Some(self.data[f])
    }
    // Bounds-checked store, aborting on out-of-range.
    fn set(&!self, int f, T v) {
        if f < 0 || f >= self.size {
            print(f"Tensor flat index out of bounds: size={self.size} index={f}")
            std.c.abort()
        }
        self.data[f] = v
    }

    // ---- multi-index access (rank-specific; `t[i,j,k]` sugar is phase 2) ----

    fn _ckdim(&self, int k, int i) {
        int d = self.shape.get!(k)
        if i < 0 || i >= d {
            print(f"Tensor index out of bounds: dim {k} len={d} index={i}")
            std.c.abort()
        }
    }
    fn at2(&self, int i, int j) -> T {
        self._ckdim(0, i)
        self._ckdim(1, j)
        return self.data[i * self.strides.get!(0) + j * self.strides.get!(1)]
    }
    fn set2(&!self, int i, int j, T v) {
        self._ckdim(0, i)
        self._ckdim(1, j)
        self.data[i * self.strides.get!(0) + j * self.strides.get!(1)] = v
    }
    fn at3(&self, int i, int j, int k) -> T {
        self._ckdim(0, i)
        self._ckdim(1, j)
        self._ckdim(2, k)
        return self.data[i * self.strides.get!(0) + j * self.strides.get!(1) + k * self.strides.get!(2)]
    }
    fn set3(&!self, int i, int j, int k, T v) {
        self._ckdim(0, i)
        self._ckdim(1, j)
        self._ckdim(2, k)
        self.data[i * self.strides.get!(0) + j * self.strides.get!(1) + k * self.strides.get!(2)] = v
    }
    fn at4(&self, int i, int j, int k, int l) -> T {
        self._ckdim(0, i)
        self._ckdim(1, j)
        self._ckdim(2, k)
        self._ckdim(3, l)
        return self.data[i * self.strides.get!(0) + j * self.strides.get!(1)
                       + k * self.strides.get!(2) + l * self.strides.get!(3)]
    }
    fn set4(&!self, int i, int j, int k, int l, T v) {
        self._ckdim(0, i)
        self._ckdim(1, j)
        self._ckdim(2, k)
        self._ckdim(3, l)
        self.data[i * self.strides.get!(0) + j * self.strides.get!(1)
                + k * self.strides.get!(2) + l * self.strides.get!(3)] = v
    }

    // Multi-subscript index protocol (reserved methods, arity-dispatched by the
    // compiler): `t[i,j]` -> __index2, `t[i,j,k]` -> __index3, `t[i,j,k,l]` ->
    // __index4 (and the __index_set{N} mirror for `t[...] = v`). Thin wrappers over
    // atN/setN — fixed arity, scalar args, no container, so the offset arithmetic
    // inlines. >4-D uses the flat get!/set! escape hatch. Bounds-checked (atN).
    fn __index2(&self, int i, int j) -> T { return self.at2(i, j) }
    fn __index3(&self, int i, int j, int k) -> T { return self.at3(i, j, k) }
    fn __index4(&self, int i, int j, int k, int l) -> T { return self.at4(i, j, k, l) }
    fn __index_set2(&!self, int i, int j, T v) { self.set2(i, j, v) }
    fn __index_set3(&!self, int i, int j, int k, T v) { self.set3(i, j, k, v) }
    fn __index_set4(&!self, int i, int j, int k, int l, T v) { self.set4(i, j, k, l, v) }

    // ---- numerical ops (phase 3a) — all build on flat access + strides ----
    //
    // Element type T must support the arithmetic used (+ - * / on numeric T,
    // > for max/argmax/relu, and math.exp for exp/sigmoid/softmax — float only).
    // These are generic methods, monomorphized per element type, so the
    // requirement is only enforced when actually used with a given T (lazy, like
    // Vec's where T: Ord). Elementwise binary ops follow NumPy broadcasting.

    // Broadcast plan: align self/o shapes from the trailing axis (NumPy rules —
    // dims must be equal or one is 1; missing leading dims act as 1). Returns the
    // result shape + each operand's broadcast strides aligned to the result rank
    // (stride 0 on broadcast axes so reads "stretch"). Incompatible shapes abort.
    fn _bcast_plan(&self, &Tensor(T) o) -> BCast {
        int ra = self.shape.len()
        int rb = o.shape.len()
        int rr = ra
        if rb > rr { rr = rb }
        Vec(int) osh = {}
        Vec(int) sa = {}
        Vec(int) sb = {}
        int z = 0
        while z < rr { osh.push(0); sa.push(0); sb.push(0); z = z + 1 }
        int ax = ra - 1
        int bx = rb - 1
        int rk = rr - 1
        while rk >= 0 {
            int da = 1
            int db = 1
            int stra = 0
            int strb = 0
            if ax >= 0 { da = self.shape.get!(ax); stra = self.strides.get!(ax) }
            if bx >= 0 { db = o.shape.get!(bx); strb = o.strides.get!(bx) }
            int dr = da
            if da != db {
                if da == 1 { dr = db } else {
                    if db == 1 { dr = da } else {
                        print(f"Tensor broadcast incompatible: {da} vs {db}")
                        std.c.abort()
                    }
                }
            }
            osh.set(rk, dr)
            if da == 1 { sa.set(rk, 0) } else { sa.set(rk, stra) }
            if db == 1 { sb.set(rk, 0) } else { sb.set(rk, strb) }
            ax = ax - 1
            bx = bx - 1
            rk = rk - 1
        }
        int size = 1
        int t = 0
        while t < rr { size = size * osh.get!(t); t = t + 1 }
        return BCast{ shape: osh, sa: sa, sb: sb, size: size }
    }

    // Apply a binary op with NumPy broadcasting: opcode 0=add 1=sub 2=mul 3=div.
    // Decodes each result flat index into a multi-index and reads each operand
    // through its broadcast strides (stretching size-1 / missing axes).
    fn _bcast(&self, &Tensor(T) o, int opcode) -> Tensor(T) {
        BCast bc = self._bcast_plan(o)
        Tensor(T) out = {}
        int total = out._setup(bc.shape);
        *T p = std.c.malloc(total * sizeof(T)) as *T
        int rank = bc.shape.len()
        int i = 0
        while i < total {
            int rem = i
            int oa = 0
            int ob = 0
            int k = rank - 1
            while k >= 0 {
                int dimk = bc.shape.get!(k)
                int idx = rem % dimk
                rem = rem / dimk
                oa = oa + idx * bc.sa.get!(k)
                ob = ob + idx * bc.sb.get!(k)
                k = k - 1
            }
            T av = self.data[oa]
            T bv = o.data[ob]
            T rv = T.zero()
            if opcode == 0 { rv = av + bv }
            if opcode == 1 { rv = av - bv }
            if opcode == 2 { rv = av * bv }
            if opcode == 3 { rv = av / bv }
            p[i] = rv
            i = i + 1
        }
        out.data = p
        return out
    }

    // Elementwise binary ops with NumPy broadcasting (mul is Hadamard, NOT matmul).
    // Same-shape is the common case; bias-style `[m,n] + [n]` broadcasts the bias
    // across rows.
    fn add(&self, &Tensor(T) o) -> Tensor(T) { return self._bcast(o, 0) }
    fn sub(&self, &Tensor(T) o) -> Tensor(T) { return self._bcast(o, 1) }
    fn mul(&self, &Tensor(T) o) -> Tensor(T) { return self._bcast(o, 2) }
    fn div(&self, &Tensor(T) o) -> Tensor(T) { return self._bcast(o, 3) }

    // Scalar broadcast: every element OP s → new Tensor of the same shape.
    fn add_scalar(&self, T s) -> Tensor(T) {
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        int i = 0
        while i < n { p[i] = self.data[i] + s; i = i + 1 }
        out.data = p
        return out
    }
    fn scale(&self, T s) -> Tensor(T) {            // multiply every element by s
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        int i = 0
        while i < n { p[i] = self.data[i] * s; i = i + 1 }
        out.data = p
        return out
    }

    // Reductions over all elements (flat).
    fn sum(&self) -> T {
        T acc = T.zero()
        int i = 0
        while i < self.size { acc = acc + self.data[i]; i = i + 1 }
        return acc
    }
    fn mean(&self) -> T { return self.sum() / (self.size as T) }
    fn max(&self) -> T {                            // assumes size >= 1
        T m = self.data[0]
        int i = 1
        while i < self.size {
            T v = self.data[i]
            if v > m { m = v }
            i = i + 1
        }
        return m
    }
    fn argmax(&self) -> int {                       // flat index of the max element
        int idx = 0
        T m = self.data[0]
        int i = 1
        while i < self.size {
            T v = self.data[i]
            if v > m { m = v; idx = i }
            i = i + 1
        }
        return idx
    }

    // ReLU: max(0, x) elementwise → new Tensor of the same shape.
    fn relu(&self) -> Tensor(T) {
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        T z = T.zero()
        int i = 0
        while i < n {
            T v = self.data[i]
            if v > z { p[i] = v } else { p[i] = z }
            i = i + 1
        }
        out.data = p
        return out
    }

    // Sum-reduce one axis → a tensor of rank-1 lower (the `axis` dim removed).
    // e.g. [m,n].sum_axis(0) → [n] (column sums); .sum_axis(1) → [m] (row sums).
    // A 1-D input reduces to a rank-0 (scalar) tensor holding the total.
    fn sum_axis(&self, int axis) -> Tensor(T) {
        int r = self.shape.len()
        if axis < 0 || axis >= r {
            print(f"Tensor.sum_axis bad axis {axis} for rank {r}")
            std.c.abort()
        }
        Vec(int) osh = {}
        int k0 = 0
        while k0 < r { if k0 != axis { osh.push(self.shape.get!(k0)) } k0 = k0 + 1 }
        Tensor(T) out = {}
        int total = out._setup(osh);
        *T p = std.c.malloc(total * sizeof(T)) as *T
        int zz = 0
        while zz < total { p[zz] = T.zero(); zz = zz + 1 }
        int n = self.size
        int i = 0
        while i < n {
            int rem = i
            int outflat = 0
            int kk = r - 1
            while kk >= 0 {
                int dimk = self.shape.get!(kk)
                int idx = rem % dimk
                rem = rem / dimk
                if kk != axis {
                    int oaxis = kk
                    if kk > axis { oaxis = kk - 1 }
                    outflat = outflat + idx * out.strides.get!(oaxis)
                }
                kk = kk - 1
            }
            p[outflat] = p[outflat] + self.data[i]
            i = i + 1
        }
        out.data = p
        return out
    }

    // Mean over one axis (= sum_axis / axis-length). For integer T this is an
    // integer (truncating) mean, matching int division elsewhere.
    fn mean_axis(&self, int axis) -> Tensor(T) {
        Tensor(T) s = self.sum_axis(axis)
        T al = self.dim(axis) as T
        int i = 0
        while i < s.size { s.set!(i, s.get!(i) / al); i = i + 1 }
        return s
    }

    // Max over one axis → a tensor of rank-1 lower (the `axis` dim removed).
    fn max_axis(&self, int axis) -> Tensor(T) {
        int r = self.shape.len()
        if axis < 0 || axis >= r {
            print(f"Tensor.max_axis bad axis {axis} for rank {r}")
            std.c.abort()
        }
        Vec(int) osh = {}
        int k0 = 0
        while k0 < r { if k0 != axis { osh.push(self.shape.get!(k0)) } k0 = k0 + 1 }
        Tensor(T) out = {}
        int total = out._setup(osh);
        *T p = std.c.malloc(total * sizeof(T)) as *T
        Vec(bool) seen = {}
        int zz = 0
        while zz < total { seen.push(false); p[zz] = T.zero(); zz = zz + 1 }
        int n = self.size
        int i = 0
        while i < n {
            int rem = i
            int outflat = 0
            int kk = r - 1
            while kk >= 0 {
                int dimk = self.shape.get!(kk)
                int idx = rem % dimk
                rem = rem / dimk
                if kk != axis {
                    int oaxis = kk
                    if kk > axis { oaxis = kk - 1 }
                    outflat = outflat + idx * out.strides.get!(oaxis)
                }
                kk = kk - 1
            }
            T v = self.data[i]
            if seen.get!(outflat) {
                if v > p[outflat] { p[outflat] = v }
            } else {
                p[outflat] = v
                seen.set!(outflat, true)
            }
            i = i + 1
        }
        out.data = p
        return out
    }

    // ---- float activations (use math.exp/tanh; element type must be float) ----
    //
    // exp / sigmoid / tanh / softmax_rows call into math.*. These are generic
    // methods instantiated in the CALLER's module, but the caller need NOT
    // `import math`: the compiler resolves `math` as an ambient builtin module
    // when it is not shadowed by a local (mirroring the ambient std.c.* path).

    // Elementwise e^x → new Tensor of the same shape.
    fn exp(&self) -> Tensor(T) {
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        int i = 0
        while i < n { p[i] = math.exp(self.data[i]); i = i + 1 }
        out.data = p
        return out
    }

    // Logistic sigmoid 1/(1+e^-x) elementwise.
    fn sigmoid(&self) -> Tensor(T) {
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        T one = 1 as T
        T zero = T.zero()
        int i = 0
        while i < n {
            T x = self.data[i]
            p[i] = one / (one + math.exp(zero - x))
            i = i + 1
        }
        out.data = p
        return out
    }

    // Hyperbolic tangent elementwise.
    fn tanh(&self) -> Tensor(T) {
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        int i = 0
        while i < n { p[i] = math.tanh(self.data[i]); i = i + 1 }
        out.data = p
        return out
    }

    // Row-wise softmax over a 2-D [m,n] tensor (each row sums to 1). Numerically
    // stable (subtracts the per-row max before exp). The canonical classifier head.
    fn softmax_rows(&self) -> Tensor(T) {
        if self.shape.len() != 2 {
            print(f"Tensor.softmax_rows requires 2-D (got rank {self.shape.len()})")
            std.c.abort()
        }
        int m = self.shape.get!(0)
        int n = self.shape.get!(1)
        int s0 = self.strides.get!(0)
        int s1 = self.strides.get!(1)
        Tensor(T) out = {}
        int total = out._setup(self.shape);
        *T p = std.c.malloc(total * sizeof(T)) as *T
        int r = 0
        while r < m {
            T mx = self.data[r * s0]
            int c = 1
            while c < n {
                T v = self.data[r * s0 + c * s1]
                if v > mx { mx = v }
                c = c + 1
            }
            T acc = T.zero()
            c = 0
            while c < n {
                T e = math.exp(self.data[r * s0 + c * s1] - mx)
                p[r * n + c] = e
                acc = acc + e
                c = c + 1
            }
            c = 0
            while c < n { p[r * n + c] = p[r * n + c] / acc; c = c + 1 }
            r = r + 1
        }
        out.data = p
        return out
    }

    // 2-D matrix multiply: self [m,k] @ o [k,n] → [m,n]. Strided reads on the
    // operands (works on reshaped / future strided views); the result is fresh
    // contiguous row-major.
    fn matmul(&self, &Tensor(T) o) -> Tensor(T) {
        if self.shape.len() != 2 || o.shape.len() != 2 {
            print(f"Tensor.matmul requires 2-D operands (got rank {self.shape.len()} and {o.shape.len()})")
            std.c.abort()
        }
        int m = self.shape.get!(0)
        int k = self.shape.get!(1)
        int k2 = o.shape.get!(0)
        int nn = o.shape.get!(1)
        if k != k2 {
            print(f"Tensor.matmul inner dim mismatch: {k} vs {k2}")
            std.c.abort()
        }
        int sa0 = self.strides.get!(0)
        int sa1 = self.strides.get!(1)
        int sb0 = o.strides.get!(0)
        int sb1 = o.strides.get!(1)
        Tensor(T) out = {}
        Vec(int) osh = {}
        osh.push(m)
        osh.push(nn)
        int total = out._setup(osh);
        *T p = std.c.malloc(total * sizeof(T)) as *T
        int r = 0
        while r < m {
            int cc = 0
            while cc < nn {
                T acc = T.zero()
                int t = 0
                while t < k {
                    T av = self.data[r * sa0 + t * sa1]
                    T bv = o.data[t * sb0 + cc * sb1]
                    acc = acc + av * bv
                    t = t + 1
                }
                p[r * nn + cc] = acc
                cc = cc + 1
            }
            r = r + 1
        }
        out.data = p
        return out
    }

    // 2-D transpose [m,n] → [n,m] (materialized copy; a strided view comes in phase 2).
    fn transpose(&self) -> Tensor(T) {
        if self.shape.len() != 2 {
            print(f"Tensor.transpose requires 2-D (got rank {self.shape.len()})")
            std.c.abort()
        }
        int m = self.shape.get!(0)
        int nn = self.shape.get!(1)
        Tensor(T) out = {}
        Vec(int) osh = {}
        osh.push(nn)
        osh.push(m)
        int total = out._setup(osh);
        *T p = std.c.malloc(total * sizeof(T)) as *T
        int i = 0
        while i < m {
            int j = 0
            while j < nn {
                p[j * m + i] = self.at2(i, j)     // out[j,i] = self[i,j]
                j = j + 1
            }
            i = i + 1
        }
        out.data = p
        return out
    }

    // ---- safe copying slices (own the result; no view/lifetime footgun) ----

    // Copy row i of a 2-D tensor → a fresh 1-D [cols] tensor.
    fn row(&self, int i) -> Tensor(T) {
        if self.shape.len() != 2 { print(f"Tensor.row requires 2-D"); std.c.abort() }
        self._ckdim(0, i)
        int n = self.shape.get!(1)
        int s0 = self.strides.get!(0)
        int s1 = self.strides.get!(1)
        Vec(int) osh = {}
        osh.push(n)
        Tensor(T) out = {}
        int total = out._setup(osh);
        *T p = std.c.malloc(total * sizeof(T)) as *T
        int j = 0
        while j < n { p[j] = self.data[i * s0 + j * s1]; j = j + 1 }
        out.data = p
        return out
    }

    // Copy column j of a 2-D tensor → a fresh 1-D [rows] tensor.
    fn col(&self, int j) -> Tensor(T) {
        if self.shape.len() != 2 { print(f"Tensor.col requires 2-D"); std.c.abort() }
        self._ckdim(1, j)
        int m = self.shape.get!(0)
        int s0 = self.strides.get!(0)
        int s1 = self.strides.get!(1)
        Vec(int) osh = {}
        osh.push(m)
        Tensor(T) out = {}
        int total = out._setup(osh);
        *T p = std.c.malloc(total * sizeof(T)) as *T
        int i = 0
        while i < m { p[i] = self.data[i * s0 + j * s1]; i = i + 1 }
        out.data = p
        return out
    }

    // Copy [lo, hi) along `axis` → a fresh tensor of the same rank (axis dim = hi-lo).
    fn slice(&self, int axis, int lo, int hi) -> Tensor(T) {
        int r = self.shape.len()
        if axis < 0 || axis >= r { print(f"Tensor.slice bad axis {axis}"); std.c.abort() }
        int alen = self.shape.get!(axis)
        if lo < 0 || hi > alen || lo >= hi {
            print(f"Tensor.slice bad range [{lo},{hi}) for axis len {alen}")
            std.c.abort()
        }
        Vec(int) osh = {}
        int k = 0
        while k < r {
            if k == axis { osh.push(hi - lo) } else { osh.push(self.shape.get!(k)) }
            k = k + 1
        }
        Tensor(T) out = {}
        int total = out._setup(osh);
        *T p = std.c.malloc(total * sizeof(T)) as *T
        int i = 0
        while i < total {
            int rem = i
            int inoff = 0
            int kk = r - 1
            while kk >= 0 {
                int odim = osh.get!(kk)
                int idx = rem % odim
                rem = rem / odim
                int incoord = idx
                if kk == axis { incoord = idx + lo }
                inoff = inoff + incoord * self.strides.get!(kk)
                kk = kk - 1
            }
            p[i] = self.data[inoff]
            i = i + 1
        }
        out.data = p
        return out
    }

    // Per-row argmax of a 2-D tensor → Vec(int) of m class indices (classifier head).
    fn argmax_rows(&self) -> Vec(int) {
        if self.shape.len() != 2 { print(f"Tensor.argmax_rows requires 2-D"); std.c.abort() }
        int m = self.shape.get!(0)
        int n = self.shape.get!(1)
        int s0 = self.strides.get!(0)
        int s1 = self.strides.get!(1)
        Vec(int) out = {}
        int r = 0
        while r < m {
            int best = 0
            T bv = self.data[r * s0]
            int c = 1
            while c < n {
                T v = self.data[r * s0 + c * s1]
                if v > bv { bv = v; best = c }
                c = c + 1
            }
            out.push(best)
            r = r + 1
        }
        return out
    }

    // Min over all elements (flat); complement to max.
    fn min(&self) -> T {
        T m = self.data[0]
        int i = 1
        while i < self.size {
            T v = self.data[i]
            if v < m { m = v }
            i = i + 1
        }
        return m
    }

    // ---- elementwise unary → new Tensor of the same shape ----
    fn neg(&self) -> Tensor(T) {
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        T z = T.zero()
        int i = 0
        while i < n { p[i] = z - self.data[i]; i = i + 1 }
        out.data = p
        return out
    }
    fn abs(&self) -> Tensor(T) {
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        T z = T.zero()
        int i = 0
        while i < n {
            T v = self.data[i]
            if v < z { p[i] = z - v } else { p[i] = v }
            i = i + 1
        }
        out.data = p
        return out
    }
    fn sqrt(&self) -> Tensor(T) {          // float only (math.sqrt)
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        int i = 0
        while i < n { p[i] = math.sqrt(self.data[i]); i = i + 1 }
        out.data = p
        return out
    }
    fn log(&self) -> Tensor(T) {           // natural log, float only (math.log)
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        int i = 0
        while i < n { p[i] = math.log(self.data[i]); i = i + 1 }
        out.data = p
        return out
    }
    // Clamp every element to [lo, hi].
    fn clamp(&self, T lo, T hi) -> Tensor(T) {
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        int i = 0
        while i < n {
            T v = self.data[i]
            if v < lo { v = lo }
            if v > hi { v = hi }
            p[i] = v
            i = i + 1
        }
        out.data = p
        return out
    }

    // Mean squared error against another tensor (same shape or broadcastable):
    // mean((self - o)^2). A basic regression loss.
    fn mse(&self, &Tensor(T) o) -> T {
        Tensor(T) d = self.sub(o)
        T acc = T.zero()
        int n = d.size
        int i = 0
        while i < n { T x = d.data[i]; acc = acc + x * x; i = i + 1 }
        return acc / (n as T)
    }

    // ---- reshape: same total size, recompute strides; NO data move ----
    fn reshape(&!self, &Vec(int) newshape) {
        int r = newshape.len()
        int n = 1
        int k = 0
        while k < r { n = n * newshape.get!(k); k = k + 1 }
        if n != self.size {
            print(f"Tensor.reshape size mismatch: have {self.size} want {n}")
            std.c.abort()
        }
        self._setup(newshape)     // resets shape/strides/size (size unchanged)
    }

    // ---- iteration over flat elements (row-major storage order) ----
    fn iter(&self) -> TensorIter(T) {
        return TensorIter(T){ data: self.data, size: self.size, i: 0 }
    }

    // ---- copy / clone / drop ----

    // Independent deep copy (data buffer + shape/strides metadata).
    fn copy(&self) -> Tensor(T) {
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        int i = 0
        while i < n { p[i] = self.data[i]; i = i + 1 }   // POD element copy
        out.data = p
        return out
    }
    // User deep-copy hook (used when a Tensor is read by value as a nested element).
    fn __clone(&self) -> Tensor(T) { return self.copy() }

    // Free the data buffer. The shape/strides Vec fields are auto-dropped by the
    // compiler-generated __drop wrapper after this body runs.
    fn __drop() { std.c.free(self.data as *u8) }
}

// Borrowing iterator over a Tensor's flat elements. Holds a RAW pointer into the
// tensor's buffer — does NOT own it, so must not outlive the tensor. Produced by
// Tensor.iter(); driven by the `for v in t` desugaring.
struct TensorIter(T) { *T data; int size; int i }

impl(T) TensorIter(T) {
    fn next(&!self) -> Option(T) {
        if self.i >= self.size { return None }
        T e = self.data[self.i]          // clone-on-read (POD: value copy)
        self.i = self.i + 1
        return Some(e)
    }
}
