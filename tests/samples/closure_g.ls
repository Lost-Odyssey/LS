// Phase G — Block env deep-clone (resolves L-007).
// Copying a Block out of a vec element / struct field / map value deep-clones
// the closure env, so the destination owns an independent env (no shared-env
// double-free). Self-verifying: prints "G PASS" only if every check holds.
//
// F5 (VR-LIM-017 resolved): now uses std.vec Vec(Block). The full Block
// lifecycle through a pure-LS container works — push moves the closure env in,
// get/[i] deep-clones it out at the bind site, and Vec drop frees element envs.

import std.vec

type Fn = Block(int) -> int

struct Holder {
    Fn op
}

struct Tag {
    string name
}

fn check(bool cond, int id) -> bool {
    if !cond {
        print(id)        // print the failing check id for diagnosis
        print("G FAIL")
    }
    return cond
}

fn main() {
    bool ok = true

    // --- 1) POD capture extracted from a vec, called repeatedly ---
    int base = 100
    Vec(Fn) fns = {}
    fns.push(|x| x + base)
    fns.push(|x| x * 2)
    Fn g = fns[0]
    ok = check(g(5) == 105, 1) && ok
    ok = check(g(7) == 107, 2) && ok
    ok = check(fns[0](2) == 102, 3) && ok    // original element still valid
    Fn h = fns[1]                            // no-capture: env NULL, clone no-op
    ok = check(h(8) == 16, 4) && ok

    // --- 2) string capture extracted from a struct field ---
    string prefix = "val="                   // length 4
    Holder hold = Holder { op: |x| x + prefix.length as int }
    Fn sf = hold.op
    ok = check(sf(10) == 14, 5) && ok
    ok = check(sf(20) == 24, 6) && ok

    // --- 3) has_drop struct capture extracted from a vec ---
    Tag t = Tag { name: "kg" }               // length 2
    Vec(Fn) sfns = {}
    sfns.push(|x| x + t.name.length as int)
    Fn a = sfns[0]
    Fn b = sfns[0]                           // two independent clones, same element
    ok = check(a(1) == 3, 7) && ok
    ok = check(b(100) == 102, 8) && ok
    ok = check(sfns[0](3) == 5, 9) && ok

    // --- 4) capture extracted from a map value ---
    int k = 3
    map(string, Fn) tbl = {}
    tbl.set("add_k", |x| x + k)
    Fn mf = tbl.get("add_k")
    ok = check(mf(1) == 4, 10) && ok
    ok = check(mf(2) == 5, 11) && ok

    // --- 5) reassignment path (AST_ASSIGN clones too) ---
    Fn cur = |x| x
    cur = fns[0]
    ok = check(cur(5) == 105, 12) && ok

    if ok { print("G PASS") }
}
