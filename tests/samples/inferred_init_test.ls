// inferred_init_test.ls — C++-style inferred aggregate init: `Type v = {}`
// zero-initializes a struct, inferring the type from the declared LHS (no type
// prefix needed). Unspecified fields are zero. Empty map literal still works
// (struct inference only triggers when the declared type is a struct).
// Prints "ok <label>" / "FAIL <label>" then "INIT PASS".

import std.rawvec

struct Point { int x; int y; int z }
struct Mixed { int n; bool flag; *int ptr }

fn check(bool c, string l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

// anon partial literal in return position (type inferred from -> Point)
fn mk_point() -> Point { return { x: 7 } }
// anon literal in a generic body (cloned at monomorphization)
fn take_x(Point p) -> int { return p.x }

fn main() {
    // POD struct zero-init via inferred {}
    Point p = {}
    check(p.x == 0 && p.y == 0 && p.z == 0, "Point {} zero-init")

    // inferred PARTIAL init: unspecified fields zero
    Point a = { x: 5 }
    check(a.x == 5 && a.y == 0 && a.z == 0, "{ x:5 } partial")
    Point b = { x: 1, z: 9 }
    check(b.x == 1 && b.y == 0 && b.z == 9, "{ x:1, z:9 } partial")

    // return position
    Point r = mk_point()
    check(r.x == 7 && r.y == 0, "return { x:7 }")

    // argument position
    check(take_x({ x: 11 }) == 11, "arg { x:11 }")

    // mixed POD/pointer struct: int 0, bool false, ptr null
    // (NOTE: zero-init of a `string` FIELD yields null-data, not a valid "" —
    //  a pre-existing struct-literal limitation, orthogonal to inferred init;
    //  RawVec's fields are *T/int/int so it is unaffected.)
    Mixed m = {}
    check(m.n == 0, "Mixed {} int zero")
    check(m.flag == false, "Mixed {} bool false")

    // RawVec(string) v = {} replaces new_rawvec(string)() — matches vec(T) v = []
    RawVec(string) v = {}
    check(v.length() == 0, "RawVec {} empty")
    v.push(f"a"); v.push(f"b")
    check(v.length() == 2, "RawVec {} push works")
    check(v.get(0) == "a", "RawVec {} get(0)")

    // RawVec(int) too
    RawVec(int) vi = {}
    for (int i = 0; i < 5; i = i + 1) { vi.push(i * i) }
    check(vi.length() == 5 && vi.get(4) == 16, "RawVec(int) {} works")

    // ---- list-literal init (the __from_list protocol; matches vec(T) v = [..]) ----
    RawVec(int) li = [10, 20, 30, 40]
    check(li.length() == 4 && li.get(0) == 10 && li.get(3) == 40, "RawVec(int) = [..]")
    RawVec(string) ls = [f"a", f"b", f"c"]
    check(ls.length() == 3 && ls.get(1) == "b", "RawVec(string) = [..]")
    RawVec(int) le = []
    check(le.length() == 0, "RawVec = [] empty")

    // empty map literal still resolves via declared map type (not hijacked)
    map(string, int) mp = {}
    mp.set("k", 42)
    int got = mp.get("k")
    check(got == 42, "map {} still works")

    print("INIT PASS")
}
