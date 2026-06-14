// Phase 2 (borrow extension, docs/plan_borrow_extension.md §3): return borrows
// with single-input lifetime elision — an `&self`/`&!self` method may return a
// borrow derived from self (`return self.field`). Covers immediate use, binding
// to a Phase-1 local borrow, writable borrow return + mutation, and a has_drop
// (Str) field. Marker driver: "BR PASS" lines, never "FAIL"; memcheck 0/0/0.
import std.str

struct Inner { int v; Str name }
struct Outer { Inner inner }

impl Outer {
    fn get(&self) -> &Inner { return self.inner }
    fn get_mut(&!self) -> &!Inner { return self.inner }
}

fn check_immediate() {
    Outer o = Outer{inner: Inner{v: 42, name: "hi"}}
    // The returned borrow is used immediately (a temporary; no escape).
    if (o.get().v == 42) { print("BR PASS imm") } else { print("BR FAIL imm") }
}

fn check_bind() {
    Outer o = Outer{inner: Inner{v: 7, name: "hello"}}
    &Inner r = o.get()
    if (r.v == 7 && r.name.len() == 5) { print("BR PASS bind") } else { print("BR FAIL bind") }
}

fn check_write() {
    Outer o = Outer{inner: Inner{v: 1, name: "x"}}
    &!Inner m = o.get_mut()
    m.v = 99
    // Mutation through the returned writable borrow reaches the referent.
    if (o.inner.v == 99) { print("BR PASS write") } else { print("BR FAIL write") }
}

fn main() -> int {
    check_immediate()
    check_bind()
    check_write()
    print("BR PASS")
    return 0
}
