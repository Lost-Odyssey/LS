// rawvec_poc_test.ls — Step 4 / Gate M0: a hand-written self-managed dynamic
// array over raw malloc/realloc/free, using the three Step 1-3 primitives:
//   realloc (grow), sizeof(int) (byte count), p[i] (typed index read/write).
// POD element (int) only — no has_drop element ownership yet (that is Step 5).
// __drop frees the buffer exactly once. memcheck must be 0/0/0 even though push
// triggers several realloc migrations (0 -> 4 -> 8 -> 16 -> 32).
// Prints "ok <label>" / "FAIL <label>" then "RAWPOC PASS".

struct RawVecI {
    *int data
    int  len
    int  cap
}

fn new_rawveci() -> RawVecI {
    // a nil pointer must be bound to a local *int before use in a literal
    *int p = nil
    return RawVecI { data: p, len: 0, cap: 0 }
}

impl RawVecI {
    fn push(&!self, int x) {
        if self.len >= self.cap {
            int ncap = 4
            if self.cap > 0 { ncap = self.cap * 2 }
            // grow: realloc old buffer to ncap elements (NULL data -> malloc)
            self.data = realloc(self.data as *u8, ncap * sizeof(int)) as *int
            self.cap = ncap
        }
        self.data[self.len] = x      // raw typed store
        self.len = self.len + 1
    }

    fn get(&self, int i) -> int { return self.data[i] }
    fn length(&self) -> int { return self.len }
    fn capacity(&self) -> int { return self.cap }

    fn __drop() {
        // POD elements: nothing to drop per-element; just free the buffer once.
        if self.cap > 0 { free(self.data as *u8) }
    }
}

fn check(bool c, string l) {
    if c { print(f"ok {l}") } else { print(f"FAIL {l}") }
}

fn main() {
    RawVecI v = new_rawveci()
    check(v.length() == 0, "empty len 0")
    check(v.capacity() == 0, "empty cap 0")

    // push 20 squares -> capacity grows 0,4,8,16,32
    for (int i = 0; i < 20; i = i + 1) { v.push(i * i) }
    check(v.length() == 20, "len 20 after 20 push")
    check(v.capacity() == 32, "cap grew to 32")

    // read back
    int sum = 0
    for (int i = 0; i < v.length(); i = i + 1) { sum = sum + v.get(i) }
    check(sum == 2470, "sum of squares 0..19 = 2470")
    check(v.get(0) == 0, "v[0] = 0")
    check(v.get(5) == 25, "v[5] = 25")
    check(v.get(19) == 361, "v[19] = 361")

    // overwrite via raw store path (set)
    v.data[5] = 999
    check(v.get(5) == 999, "v[5] overwritten = 999")

    // a second short-lived RawVecI to exercise an extra alloc/free cycle
    RawVecI w = new_rawveci()
    w.push(7)
    w.push(8)
    check(w.get(0) + w.get(1) == 15, "second vec sum = 15")

    print("RAWPOC PASS")
    // v and w go out of scope here -> __drop frees both buffers exactly once
}
