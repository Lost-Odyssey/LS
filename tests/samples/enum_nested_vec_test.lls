// enum_nested_vec_test.ls
// Phase E-3 TDD: nested has-drop enum (enum containing vec of enum)
// Verifies deep drop/clone chains work correctly end-to-end
// Expected: 0 leaks, 0 double-frees with --memcheck

import std.core.vec
import std.core.str

enum JVal {
    JNull
    JStr(Str s)
    JArr(Vec(JVal) items)
}

def owned(Str x) -> Str { return x.copy() }

def make_arr() -> JVal {
    Vec(JVal) items = [JStr(owned("a")), JStr(owned("b"))]
    return JArr(items)
}

def main() {
    // A: nested construction + scope drop
    Vec(JVal) inner = [JStr(owned("x")), JStr(owned("y"))]
    JVal arr = JArr(inner)
    Vec(JVal) outer = {}
    outer.push(arr)
    @print("PASS 1: outer len =", outer.len())
    // outer → JArr → vec → JStr → Str: full chain drop

    // B: copy nested structure (deep clone of enum containing vec)
    Vec(JVal) outer2 = outer.copy()
    @print("PASS 2: copy len =", outer2.len())
    // outer and outer2 fully independent

    // C: index read (deep clone via AST_INDEX)
    JVal elem = outer[0]
    @print("PASS 3: index read done")
    // elem is a deep clone; outer still owns its copy

    // D: var_decl clone of nested enum (Bug #13 fix)
    JVal src = make_arr()
    JVal dst = src
    @print("PASS 4: var_decl clone done")
    // src and dst independent — both JArr payloads own separate vecs

    // E: reassignment of nested enum
    JVal e = JStr(owned("old"))
    e = make_arr()
    @print("PASS 5: reassign done")
    // "old" Str dropped before reassign

    // F: vec of nested enum copy
    Vec(JVal) nested_vec = [make_arr(), make_arr()]
    Vec(JVal) nested_copy = nested_vec.copy()
    @print("PASS 6: nested vec copy len =", nested_copy.len())

    @print("all done")
}
