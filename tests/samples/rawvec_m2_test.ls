// rawvec_m2_test.ls — Step 6 / Gate M2: generic std.rawvec RawVec(T) under
// monomorphization, across element types, matching builtin vec semantics.
// Element types: int (POD), string (has_drop scalar), Pt (has_drop struct).
// Dimensions: push+grow, get(clone), pop(move-out), set, clear, scope-drop,
// whole-container move. All paths memcheck 0/0/0.
// Prints "ok <label>" / "FAIL <label>" then "M2 PASS".

import std.rawvec

struct Pt { string tag; int v }

fn check(bool c, string l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn main() {
    // ───────── RawVec(int): POD ─────────
    RawVec(int) vi = new_rawvec(int)()
    check(vi.is_empty(), "int: empty init")
    for (int i = 0; i < 20; i = i + 1) { vi.push(i * i) }     // grows 0->4->8->16->32
    check(vi.length() == 20, "int: len 20")
    check(vi.capacity() == 32, "int: cap 32")
    int sum = 0
    for (int i = 0; i < vi.length(); i = i + 1) { sum = sum + vi.get(i) }
    check(sum == 2470, "int: sum of squares = 2470")
    match vi.pop() { Some(x) => { check(x == 361, "int: pop = 361") } None => { check(false, "int pop") } }
    vi.set(0, 999)
    check(vi.get(0) == 999, "int: set(0)=999")
    vi.clear()
    check(vi.is_empty(), "int: empty after clear")

    // ───────── RawVec(string): has_drop scalar ─────────
    RawVec(string) vs = new_rawvec(string)()
    for (int i = 0; i < 6; i = i + 1) { vs.push(f"s{i}") }
    check(vs.get(2) == "s2", "str: get(2) clone = s2")
    match vs.pop() { Some(x) => { check(x == "s5", "str: pop move-out = s5") } None => { check(false, "str pop") } }
    vs.set(1, f"NEW")
    check(vs.get(1) == "NEW", "str: set(1)=NEW")
    check(vs.length() == 5, "str: len 5")

    // whole-container move (vec b = a moves; a dead)
    RawVec(string) vs2 = vs
    check(vs2.length() == 5, "str: moved container len 5")

    // ───────── RawVec(Pt): has_drop struct (recursive element drop) ─────────
    RawVec(Pt) vp = new_rawvec(Pt)()
    for (int i = 0; i < 5; i = i + 1) {
        Pt e = Pt { tag: f"t{i}", v: i * 10 }
        vp.push(__move(e))
    }
    check(vp.length() == 5, "struct: len 5")
    // aggregate element reads (clone-on-read) + field read-through of a method result
    Pt g = vp.get(2)
    check(g.tag == "t2", "struct: get(2).tag = t2 (bind)")
    check(vp.get(0).tag == "t0", "struct: get(0).tag = t0 (rvalue field)")
    check(vp.get(4).v == 40, "struct: get(4).v = 40 (POD field)")
    match vp.pop() { Some(p) => { check(p.tag == "t4", "struct: pop().tag = t4") } None => { check(false, "struct pop") } }
    check(vp.length() == 4, "struct: len 4 after pop")

    print("M2 PASS")
    // scope exit: vi (cleared, empty buffer freed); vs2 owns the moved buffer (5
    // strings + buffer freed); vp drops 4 remaining Pt (each tag freed) + buffer.
}
