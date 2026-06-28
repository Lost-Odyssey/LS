// field_borrow_owned_local_test.ls — regression for the owned-local field-borrow
// leak. Taking `&local.field` (a struct/enum-typed field) of an OWNED local
// struct and passing it to a read-only `&T` free-function param used to deep-CLONE
// the field (emit_struct_clone_val at the AST_FIELD read site). In a loop the
// clone's drop was flushed at the loop-enclosing scope — the entry-block temp
// alloca was reused each iteration, so only the last iteration's clone was freed
// and the earlier ones LEAKED. The fix borrows the field in place via GEP (no
// clone), mirroring the &self / borrow-receiver path. The clean control is borrowing
// the WHOLE struct first (always a GEP). JIT + AOT + memcheck 0/0/0.
import std.core.str
import std.core.vec

struct TI { Str dtype; Vec(int) dims }
struct DE { Str name; TI ty }

// free fn taking a borrow of a struct-typed field
def tb(&TI t) -> int { return t.dims.len() }
// clean control: borrow the whole struct, then &d.ty inside
def tb_via(&DE d) -> int { return tb(&d.ty) }

def mk() -> Vec(DE) {
    Vec(DE) v = {}
    Vec(int) d0 = {}; d0.push(1); d0.push(96)
    v.push(DE { name: "a", ty: TI { dtype: "f32", dims: d0 } })
    Vec(int) d1 = {}; d1.push(64)
    v.push(DE { name: "b", ty: TI { dtype: "f32", dims: d1 } })
    Vec(int) d2 = {}; d2.push(7); d2.push(8); d2.push(9)
    v.push(DE { name: "c", ty: TI { dtype: "i32", dims: d2 } })
    return v
}

// --- enum field borrow (covers the TYPE_ENUM branch of the fix) ---
enum Shape { Dot; Line(Vec(int)) }
struct Node { Str tag; Shape sh }

def shape_count(&Shape s) -> int {
    match s {
        Dot => { return 0 }
        Line(pts) => { return pts.len() }
    }
}

def mk_nodes() -> Vec(Node) {
    Vec(Node) v = {}
    Vec(int) p0 = {}; p0.push(1); p0.push(2)
    v.push(Node { tag: "x", sh: Line(p0) })
    v.push(Node { tag: "y", sh: Dot })
    Vec(int) p1 = {}; p1.push(3); p1.push(4); p1.push(5); p1.push(6)
    v.push(Node { tag: "z", sh: Line(p1) })
    return v
}

def check(bool cond, Str label) {
    if (cond) { @print(f"PASS {label}") } else { @print(f"FB FAIL {label}") }
}

def main() -> int {
    // 1) owned local from vec.get! in a loop, &d.ty (struct field) -> free fn
    Vec(DE) v = mk()
    int tot = 0; int i = 0
    while i < v.len() { DE d = v.get!(i); tot = tot + tb(&d.ty); i = i + 1 }
    check(tot == 6, "owned-field-borrow-loop")   // 2 + 1 + 3

    // 2) clean control: borrow whole struct first
    Vec(DE) v2 = mk()
    int tot2 = 0; int j = 0
    while j < v2.len() { DE d = v2.get!(j); tot2 = tot2 + tb_via(&d); j = j + 1 }
    check(tot2 == 6, "owned-field-borrow-via-wrapper")

    // 3) enum-typed field borrow of an owned local in a loop
    Vec(Node) ns = mk_nodes()
    int cnt = 0; int k = 0
    while k < ns.len() { Node n = ns.get!(k); cnt = cnt + shape_count(&n.sh); k = k + 1 }
    check(cnt == 6, "owned-enum-field-borrow-loop")   // 2 + 0 + 4

    // 4) single-shot (not in a loop) owned-local field borrow — still must be clean
    DE one = v.get!(0)
    check(tb(&one.ty) == 2, "owned-field-borrow-single")

    @print("FBOWNED OK")
    return 0
}
