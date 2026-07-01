// Vec.get_ref(&self,i) -> &T: zero-copy borrow of an aggregate element (no clone),
// built on generic return-borrow elision. Validates the `*T data[i]` pointer-index
// borrow-return path on the real container. Marker driver: "VGR PASS" lines, never
// "FAIL"; memcheck 0/0/0 (the borrow must NOT double-free the element).
import std.core.vec
import std.core.str

struct P { int id; Str name }

def main() -> int {
    Vec(P) v = []
    v.push(P{id: 1, name: "alice"})
    v.push(P{id: 2, name: "bob"})
    v.push(P{id: 3, name: "carol"})

    // Immediate use of the returned borrow (temporary; no escape, no clone).
    if (v.get_ref(1).id == 2) { @print("VGR PASS imm") } else { @print("VGR FAIL imm") }

    // Bind to a Phase-1 named local borrow (pins v); read a has_drop (Str) field.
    &P r = v.get_ref(0)
    if (r.id == 1 && r.name.len() == 5) { @print("VGR PASS bind") } else { @print("VGR FAIL bind") }

    // Borrow each element in turn — no per-element clone.
    int total = 0
    for (int i = 0; i < v.len(); i = i + 1) {
        total = total + v.get_ref(i).id
    }
    if (total == 6) { @print("VGR PASS loop") } else { @print("VGR FAIL loop") }

    @print("VGR PASS")
    return 0
}
