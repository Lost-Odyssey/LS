// vec_m2_test.ls — Step 6 / Gate M2: generic std.core.vec Vec(T) under
// monomorphization, across element types, matching builtin vec semantics.
// Element types: int (POD), Str (has_drop scalar), Pt (has_drop struct).
// Dimensions: push+grow, get(clone), pop(move-out), set, clear, scope-drop,
// whole-container move. All paths memcheck 0/0/0.
// Prints "ok <label>" / "FAIL <label>" then "M2 PASS".

import std.core.vec
import std.core.str

struct Pt { Str tag; int v }

def check(bool c, Str l) { if c { @print(f"ok {l}") } else { @print(f"FAIL {l}") } }

def main() {
    // ───────── Vec(int): POD ─────────
    Vec(int) vi = {}
    check(vi.empty?, "int: empty init")
    for (int i = 0; i < 20; i = i + 1) { vi.push(i * i) }     // grows 0->4->8->16->32
    check(vi.len() == 20, "int: len 20")
    check(vi.cap() == 32, "int: cap 32")
    int sum = 0
    for (int i = 0; i < vi.len(); i = i + 1) { sum = sum + vi.get!(i) }
    check(sum == 2470, "int: sum of squares = 2470")
    match vi.pop() { Some(x) => { check(x == 361, "int: pop = 361") } None => { check(false, "int pop") } }
    vi.set(0, 999)
    check(vi.get!(0) == 999, "int: set(0)=999")
    vi.clear()
    check(vi.empty?, "int: empty after clear")

    // ───────── Vec(Str): has_drop scalar ─────────
    Vec(Str) vs = {}
    for (int i = 0; i < 6; i = i + 1) { vs.push(f"s{i}") }
    check(vs.get!(2).eq?("s2"), "str: get(2) clone = s2")
    match vs.pop() { Some(x) => { check(x.eq?("s5"), "str: pop move-out = s5") } None => { check(false, "str pop") } }
    vs.set(1, f"NEW")
    check(vs.get!(1).eq?("NEW"), "str: set(1)=NEW")
    check(vs.len() == 5, "str: len 5")

    // whole-container move (vec b = a moves; a dead)
    Vec(Str) vs2 = vs
    check(vs2.len() == 5, "str: moved container len 5")

    // ───────── Vec(Pt): has_drop struct (recursive element drop) ─────────
    Vec(Pt) vp = {}
    for (int i = 0; i < 5; i = i + 1) {
        Pt e = Pt { tag: f"t{i}", v: i * 10 }
        vp.push(__move(e))
    }
    check(vp.len() == 5, "struct: len 5")
    // aggregate element reads (clone-on-read) + field read-through of a method result
    Pt g = vp.get!(2)
    check(g.tag.eq?("t2"), "struct: get(2).tag = t2 (bind)")
    check(vp.get!(0).tag.eq?("t0"), "struct: get(0).tag = t0 (rvalue field)")
    check(vp.get!(4).v == 40, "struct: get(4).v = 40 (POD field)")
    match vp.pop() { Some(p) => { check(p.tag.eq?("t4"), "struct: pop().tag = t4") } None => { check(false, "struct pop") } }
    check(vp.len() == 4, "struct: len 4 after pop")

    @print("M2 PASS")
    // scope exit: vi (cleared, empty buffer freed); vs2 owns the moved buffer (5
    // strings + buffer freed); vp drops 4 remaining Pt (each tag freed) + buffer.
}
