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

struct Tensor(T) {
    *T data            // heap buffer of `size` T, row-major contiguous
    Vec(int) shape     // dim lengths; rank = shape.len()
    Vec(int) strides   // row-major element strides: strides[k] = prod(shape[k+1..])
    int size           // prod(shape)
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
    // Allocate shape-shaped zeros (0 as T per element).
    fn init_zeros(&!self, &Vec(int) shape) { self.init(shape, 0 as T) }

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

    // ---- numerical ops (phase 3a) — all build on flat access + strides ----
    //
    // Element type T must support the arithmetic used (+ - * / on numeric T,
    // and > for max/argmax/relu). These are generic methods, monomorphized per
    // element type, so the requirement is only enforced when actually used with
    // a given T (lazy, like Vec's where T: Ord). v1 has NO broadcasting: binary
    // elementwise ops require identical shapes (use scalar variants otherwise).

    fn _shape_eq(&self, &Tensor(T) o) -> bool {
        if self.shape.len() != o.shape.len() { return false }
        int k = 0
        while k < self.shape.len() {
            if self.shape.get!(k) != o.shape.get!(k) { return false }
            k = k + 1
        }
        return true
    }
    fn _ew_check(&self, &Tensor(T) o) {
        if !self._shape_eq(o) {
            print(f"Tensor elementwise shape mismatch (no broadcasting): size {self.size} vs {o.size}")
            std.c.abort()
        }
    }

    // Elementwise (Hadamard) tensor ⊗ tensor → new Tensor of the same shape.
    fn add(&self, &Tensor(T) o) -> Tensor(T) {
        self._ew_check(o)
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        int i = 0
        while i < n { p[i] = self.data[i] + o.data[i]; i = i + 1 }
        out.data = p
        return out
    }
    fn sub(&self, &Tensor(T) o) -> Tensor(T) {
        self._ew_check(o)
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        int i = 0
        while i < n { p[i] = self.data[i] - o.data[i]; i = i + 1 }
        out.data = p
        return out
    }
    fn mul(&self, &Tensor(T) o) -> Tensor(T) {     // elementwise, NOT matmul
        self._ew_check(o)
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        int i = 0
        while i < n { p[i] = self.data[i] * o.data[i]; i = i + 1 }
        out.data = p
        return out
    }
    fn div(&self, &Tensor(T) o) -> Tensor(T) {
        self._ew_check(o)
        Tensor(T) out = {}
        int n = out._setup(self.shape);
        *T p = std.c.malloc(n * sizeof(T)) as *T
        int i = 0
        while i < n { p[i] = self.data[i] / o.data[i]; i = i + 1 }
        out.data = p
        return out
    }

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
        T acc = 0 as T
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
        T z = 0 as T
        int i = 0
        while i < n {
            T v = self.data[i]
            if v > z { p[i] = v } else { p[i] = z }
            i = i + 1
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
                T acc = 0 as T
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
