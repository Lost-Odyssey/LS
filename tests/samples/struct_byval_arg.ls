// Regression: passing a has_drop struct BY VALUE must not double-free the
// caller-owned heap of its fields.
//
// Root cause (fixed): emit_struct_clone_val deep-cloned only string and nested
// has_drop-struct fields, leaving vec/map fields as shallow copies that shared
// the caller's buffer. The call-site clone of a struct arg therefore aliased
// the caller's vec/map heap → callee scope_drop + caller scope_drop double-free.
// Fix: emit_struct_clone_val now deep-clones vec/map (and has_drop enum) fields
// too, so the callee owns an independent copy and may drop it freely.
//
// Convention: print "<label> PASS"/"<label> FAIL"; the cmake driver asserts the
// sample emits "BYVAL PASS" and never "FAIL", under JIT + AOT + memcheck.

struct Box { vec(int) items }
struct Bag { string name  map(string,int) counts }
struct Inner { vec(int) data }
struct Wrap  { Inner inner }

// by-value struct-with-vec param, read a field
fn peek(Box b) -> int { return b.items.length }
// by-value struct-with-{string,map} param, read both fields
fn name_len(Bag g) -> int { return g.name.length + g.counts.length }
// by-value nested struct param: consume the nested field, then read it
fn use_wrap(Wrap w) -> int {
    Inner local = w.inner          // owns an independent deep copy of w.inner
    return local.data.length
}

fn check(bool cond, string label) {
    if (cond) { print(f"{label} PASS") } else { print(f"{label} FAIL") }
}

fn main() {
    // --- single + multiple by-value passes of struct{vec} ---
    vec(int) v = []
    v.push(1) v.push(2) v.push(3)
    Box bx = Box { items: v }
    int a = peek(bx)          // pass #1
    int b = peek(bx)          // pass #2 (caller's bx still owns its buffer)
    int c = peek(bx)          // pass #3
    check(a == 3 && b == 3 && c == 3, "vec_field")
    check(bx.items.length == 3, "vec_field_after")   // caller still owns bx

    // --- struct{string, map} by value, multiple passes ---
    map(string,int) m = {}
    m.set("x", 10)
    m.set("y", 20)
    Bag g = Bag { name: "hello", counts: m }
    int nl1 = name_len(g)
    int nl2 = name_len(g)
    check(nl1 == 7 && nl2 == 7, "string_map_field")   // 5 + 2
    check(g.counts.length == 2, "string_map_after")

    // --- nested struct by value, multiple passes ---
    vec(int) iv = []
    iv.push(1) iv.push(2)
    Wrap w = Wrap { inner: Inner { data: iv } }
    int d1 = use_wrap(w)
    int d2 = use_wrap(w)
    check(d1 == 2 && d2 == 2, "nested_struct")
    Inner after = w.inner                              // caller still owns w
    check(after.data.length == 2, "nested_struct_after")

    print("BYVAL PASS")
}
