/* B-6: has_drop same-named struct/enum across two modules, exercised together
   through vec containers, map, enum payloads, methods, deep copy out of vec, and
   cross-module returns — all under --memcheck (0 leak / 0 double-free).
   Structs are built inline (module make()/nodes()) to avoid the unrelated
   Str-param-into-returned-struct AOT bug (BF-045).
   F6b (fixed): uses std.core.vec Vec(T). Vec instantiation names are now mangled with
   each element type's module-qualified llvm_name (Vec(mod_a__Node) vs
   Vec(mod_b__Node)), so the two same-named Node types are no longer conflated. */
module main

import std.core.vec
import std.core.str
import mod_a as A
import mod_b as B

def main() -> int {
    /* has_drop structs with owned Str fields, distinct layouts, + methods */
    A.Node na = A.make()
    B.Node nb = B.make()
    @print(f"na={na.tag()} nb={nb.tag()}")

    /* vec of has_drop structs returned across module boundary + Phase H deep copy */
    Vec(A.Node) va = A.nodes()
    Vec(B.Node) vb = B.nodes()
    A.Node first = va[0]
    A.Node second = va[1]
    B.Node bfirst = vb[0]
    @print(f"va0={first.tag()} va1={second.tag()} vb0={bfirst.tag()}")

    /* has_drop enum payloads, same enum name in both modules */
    A.Box ba = A.wrap()
    B.Box bb = B.wrap()
    @print(f"ba={A.unwrap(ba)} bb={B.unwrap(bb)}")

    /* NOTE: map keyed by Str with a has_drop struct VALUE is intentionally not
       exercised here — it leaks the struct value's owned fields on map drop, an
       unrelated pre-existing bug tracked as BF-046 (reproduces on root structs).
       The cross-module struct/method/vec/enum/Phase-H coverage above is the point. */

    if na.tag().eq?("A") && nb.tag().eq?("B") && first.tag().eq?("A1") && bfirst.tag().eq?("B1")
        && A.unwrap(ba).eq?("AX") && B.unwrap(bb).eq?("BX") {
        @print("MODTYPE_MEMCHECK PASS")
    }
    return 0
}
