// rawvec_m1_test.ls — Step 5 / Gate M1: has_drop element ownership in a
// self-managed raw-memory container. Proves the OWNERSHIP + DROP machinery:
// move-in (push), move-out (pop), per-element recursive drop (__drop), and
// nested drop (RawVec of has_drop struct / RawVec of RawVec). memcheck 0/0/0.
//
// Confirmed memory-safe idioms (each verified individually during Step 5):
//   * push owned temp   : v.push(f"...")                — temp consumed, no caller drop
//   * push named local  : v.push(__move(local))         — explicit move marks the local
//   * string read/get   : string t = self.data[i]; return t  — var_decl deep-clones strings
//   * pop / move-out     : string o = self.data[i]; __drop_at(self.data[i]); len-=1; return o
//   * set (overwrite)    : __drop_at(self.data[i]); self.data[i] = x
//   * __drop             : for i in 0..len { __drop_at(self.data[i]) }; free(data)
//   * __drop_at recurses via emit_drop_value -> string free / struct.__drop / nested.
//
// Reads match vec[i] exactly: p[i] DEEP-CLONES owned element data (string/struct/
// has_drop), so struct element reads and field read-throughs (self.data[i].name)
// are memory-safe — see RawVecP below.
// Nested container reads ALSO match vec(vec(T)): a struct with a user `__clone(&self)
// -> Self` is deep-copied via that hook when read by value (emit_clone_value calls
// it instead of the field-wise auto-clone, which can't clone a raw *T buffer). So
// RawVecV reads its inner RawVecS elements safely — see RawVecV below.
//
// Prints "ok <label>" / "FAIL <label>" then "M1 PASS".

fn check(bool c, string l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

// ───────────────────────── RawVecS: string elements ─────────────────────────
struct RawVecS { *string data; int len; int cap }
fn new_rvs() -> RawVecS { *string p = nil; return RawVecS { data: p, len: 0, cap: 0 } }
impl RawVecS {
    fn push(&!self, string x) {
        if self.len >= self.cap {
            int n = 4
            if self.cap > 0 { n = self.cap * 2 }
            self.data = realloc(self.data as *u8, n * sizeof(string)) as *string
            self.cap = n
        }
        self.data[self.len] = x          // move-in (raw store, no drop old)
        self.len = self.len + 1
    }
    fn get(&self, int i) -> string {
        string tmp = self.data[i]        // var_decl deep-clones the slot's string
        return tmp
    }
    fn pop(&!self) -> string {
        self.len = self.len - 1
        string out = self.data[self.len] // clone out
        __drop_at(self.data[self.len])   // drop the slot original
        return out
    }
    fn set(&!self, int i, string x) {
        __drop_at(self.data[i])          // drop old element
        self.data[i] = x                 // raw store new
    }
    fn length(&self) -> int { return self.len }
    // User __clone: deep-copies the buffer so this container can be cloned (e.g.
    // when read by value as a nested element). Matches vec(vec(T)) deep-clone.
    fn __clone(&self) -> RawVecS {
        RawVecS out = new_rvs()
        for (int i = 0; i < self.len; i = i + 1) {
            string s = self.data[i]      // clone-on-read each element
            out.push(s)
        }
        return out
    }
    fn __drop() {
        for (int i = 0; i < self.len; i = i + 1) { __drop_at(self.data[i]) }
        if self.cap > 0 { free(self.data as *u8) }
    }
}

// ──────────────── RawVecP: has_drop struct elements (Person) ────────────────
struct Person { string name; int age }
struct RawVecP { *Person data; int len; int cap }
fn new_rvp() -> RawVecP { *Person p = nil; return RawVecP { data: p, len: 0, cap: 0 } }
impl RawVecP {
    fn push(&!self, Person x) {
        if self.len >= self.cap {
            int n = 4
            if self.cap > 0 { n = self.cap * 2 }
            self.data = realloc(self.data as *u8, n * sizeof(Person)) as *Person
            self.cap = n
        }
        self.data[self.len] = x
        self.len = self.len + 1
    }
    fn count(&self) -> int { return self.len }
    // Aggregate element reads now match vec[i]: a read DEEP-CLONES the element,
    // so these are memory-safe (struct read, string field read-through, POD field).
    fn get_full(&self, int i) -> Person { Person pp = self.data[i]; return pp }
    fn name_of(&self, int i) -> string { return self.data[i].name }
    fn age_of(&self, int i) -> int { return self.data[i].age }
    fn __drop() {
        // __drop_at on each struct slot recurses into Person's auto __drop, which
        // frees the `name` string field — proving recursive (nested) struct drop.
        for (int i = 0; i < self.len; i = i + 1) { __drop_at(self.data[i]) }
        if self.cap > 0 { free(self.data as *u8) }
    }
}

// ─────────────── RawVecV: nested RawVec of RawVecS (deepest) ────────────────
struct RawVecV { *RawVecS data; int len; int cap }
fn new_rvv() -> RawVecV { *RawVecS p = nil; return RawVecV { data: p, len: 0, cap: 0 } }
impl RawVecV {
    fn push(&!self, RawVecS x) {
        if self.len >= self.cap {
            int n = 2
            if self.cap > 0 { n = self.cap * 2 }
            self.data = realloc(self.data as *u8, n * sizeof(RawVecS)) as *RawVecS
            self.cap = n
        }
        self.data[self.len] = x
        self.len = self.len + 1
    }
    fn count(&self) -> int { return self.len }
    // Nested element reads are now safe & match vec(vec(T)): reading an inner
    // RawVecS by value invokes RawVecS.__clone (deep copy of its buffer), so the
    // returned local and the slot are independent.
    fn row_len(&self, int i) -> int {
        RawVecS inner = self.data[i]     // -> RawVecS.__clone (deep copy)
        return inner.length()
    }
    fn row_get(&self, int i, int j) -> string {
        RawVecS inner = self.data[i]     // -> RawVecS.__clone
        string s = inner.get(j)
        return s
    }
    fn __drop() {
        // recurses two levels: __drop_at -> RawVecS.__drop -> inner string frees
        // + inner buffer free, then this outer buffer free.
        for (int i = 0; i < self.len; i = i + 1) { __drop_at(self.data[i]) }
        if self.cap > 0 { free(self.data as *u8) }
    }
}

fn main() {
    // ---- RawVecS lifecycle (string elements: full read/write) ----
    RawVecS v = new_rvs()
    for (int i = 0; i < 6; i = i + 1) { v.push(f"s{i}") }   // grows 0->4->8
    check(v.length() == 6, "len 6 after 6 push")
    check(v.get(0) == "s0", "get(0) clone = s0")
    check(v.get(5) == "s5", "get(5) clone = s5")

    string a = v.pop()                  // move-out s5
    string b = v.pop()                  // move-out s4
    check(a == "s5", "pop -> s5")
    check(b == "s4", "pop -> s4")
    check(v.length() == 4, "len 4 after 2 pop")

    v.set(1, f"NEW")                    // drop s1, store NEW
    check(v.get(1) == "NEW", "set(1) -> NEW")

    // ---- RawVecP: has_drop struct elements (recursive struct drop) ----
    RawVecP pv = new_rvp()
    for (int i = 0; i < 5; i = i + 1) {
        Person p = Person { name: f"person-{i}", age: i * 10 }
        pv.push(__move(p))              // named local -> explicit move
    }
    check(pv.count() == 5, "RawVecP count 5")
    // aggregate element reads (now clone-on-read, matching vec[i])
    Person g = pv.get_full(2)
    check(g.name == "person-2", "get_full(2).name = person-2")
    check(pv.name_of(0) == "person-0", "name_of(0) field read-through = person-0")
    check(pv.name_of(4) == "person-4", "name_of(4) field read-through = person-4")
    check(pv.age_of(3) == 30, "age_of(3) POD field = 30")

    // ---- RawVecV: nested RawVec of RawVecS (two-level recursive drop) ----
    RawVecV vv = new_rvv()
    for (int i = 0; i < 3; i = i + 1) {
        RawVecS inner = new_rvs()
        inner.push(f"row{i}-a")
        inner.push(f"row{i}-b")
        vv.push(__move(inner))          // move the inner container in
    }
    check(vv.count() == 3, "nested outer count 3")
    // nested element reads via RawVecS.__clone (matches vec(vec(T)))
    check(vv.row_len(0) == 2, "nested row 0 len 2")
    check(vv.row_get(1, 0) == "row1-a", "nested row_get(1,0) = row1-a")
    check(vv.row_get(2, 1) == "row2-b", "nested row_get(2,1) = row2-b")

    print("M1 PASS")
    // scope exit: a,b drop (owned strings); v.__drop frees 4 remaining + buffer;
    // pv.__drop recurses into Person.__drop (frees each name); vv.__drop recurses
    // into each RawVecS.__drop (inner strings + inner buffers) then its own buffer.
}
