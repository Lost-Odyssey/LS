// Phase 2 (borrow extension, docs/plan_borrow_extension.md §3): return borrows
// with single-input lifetime elision — an `&self`/`&!self` method may return a
// borrow derived from self (`return self.field`). Covers immediate use, binding
// to a Phase-1 local borrow, writable borrow return + mutation, and a has_drop
// (Str) field. Marker driver: "BR PASS" lines, never "FAIL"; memcheck 0/0/0.
import std.core.str

struct Inner { int v; Str name }
struct Outer { Inner inner }

struct Top { Outer top_outer }

methods Outer {
    def get(&self) -> &Inner { return self.inner }
    def get_mut(&!self) -> &!Inner { return self.inner }
}

// Transitively-chained borrow return: Top.deep() forwards the borrow from a
// nested borrow-returning call (`self.top_outer.get()`), still rooted at self.
methods Top {
    def deep(&self) -> &Inner { return self.top_outer.get() }
}

// Free function with exactly one borrow parameter — the result borrows `a`.
def pick(&Inner a) -> &Inner { return a }

def check_immediate() {
    Outer o = Outer{inner: Inner{v: 42, name: "hi"}}
    // The returned borrow is used immediately (a temporary; no escape).
    if (o.get().v == 42) { @print("BR PASS imm") } else { @print("BR FAIL imm") }
}

def check_bind() {
    Outer o = Outer{inner: Inner{v: 7, name: "hello"}}
    &Inner r = o.get()
    if (r.v == 7 && r.name.len() == 5) { @print("BR PASS bind") } else { @print("BR FAIL bind") }
}

def check_write() {
    Outer o = Outer{inner: Inner{v: 1, name: "x"}}
    &!Inner m = o.get_mut()
    m.v = 99
    // Mutation through the returned writable borrow reaches the referent.
    if (o.inner.v == 99) { @print("BR PASS write") } else { @print("BR FAIL write") }
}

def check_free() {
    Inner x = Inner{v: 5, name: "world"}
    // Free-def borrow return: immediate use + bind to a Phase-1 local (pins x).
    if (pick(&x).v == 5) { @print("BR PASS free-imm") } else { @print("BR FAIL free-imm") }
    &Inner r = pick(&x)
    if (r.name.len() == 5) { @print("BR PASS free-bind") } else { @print("BR FAIL free-bind") }
}

def check_chain() {
    Top t = Top{top_outer: Outer{inner: Inner{v: 8, name: "z"}}}
    // Transitively-chained borrow return through Outer.get().
    if (t.deep().v == 8) { @print("BR PASS chain-imm") } else { @print("BR FAIL chain-imm") }
    &Inner r = t.deep()
    if (r.v == 8) { @print("BR PASS chain-bind") } else { @print("BR FAIL chain-bind") }
}

def main() -> int {
    check_immediate()
    check_bind()
    check_write()
    check_free()
    check_chain()
    @print("BR PASS")
    return 0
}
