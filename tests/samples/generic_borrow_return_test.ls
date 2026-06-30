// Generic return-borrow elision (docs/plan_borrow_extension.md "下一步"): extend
// single-input `&self` borrow-return to GENERIC methods, so a generic container
// can hand out a zero-copy `&T` instead of cloning the element.
//
// Mirrors borrow_return_test.ls but the borrow-returning method lives on a
// generic struct `Box(T)`. Covers aggregate element types (Inner struct, pointer
// ABI) — the high-value zero-copy case. POD-scalar element borrows (`-> &int`)
// are deliberately rejected (see generic_borrow_scalar_reject.ls). Marker driver:
// "GBR PASS" lines, never "FAIL"; memcheck 0/0/0.
import std.core.str

struct Inner { int v; Str name }

struct Box(T) {
    T value
}

methods Box(T) {
    def get_ref(&self) -> &T { return self.value }
    def get_mut(&!self) -> &!T { return self.value }
}

// Aggregate element: &Inner is a pointer-ABI borrow (the high-value case).
def check_aggregate() {
    Box(Inner) b = Box(Inner){ value: Inner{v: 42, name: "hi"} }
    // immediate use of the returned borrow (temporary; no escape)
    if (b.get_ref().v == 42) { @print("GBR PASS agg-imm") } else { @print("GBR FAIL agg-imm") }
    // bind to a Phase-1 named local borrow (pins b)
    &Inner r = b.get_ref()
    if (r.v == 42 && r.name.len() == 2) { @print("GBR PASS agg-bind") } else { @print("GBR FAIL agg-bind") }
}

// Writable generic borrow return: mutate the element through &!T.
def check_write() {
    Box(Inner) b = Box(Inner){ value: Inner{v: 1, name: "x"} }
    &!Inner m = b.get_mut()
    m.v = 99
    if (b.value.v == 99) { @print("GBR PASS write") } else { @print("GBR FAIL write") }
}

def main() -> int {
    check_aggregate()
    check_write()
    @print("GBR PASS")
    return 0
}
