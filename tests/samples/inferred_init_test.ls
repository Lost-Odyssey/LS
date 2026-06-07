// inferred_init_test.ls — C++-style inferred aggregate init: `Type v = {}`
// zero-initializes a struct, inferring the type from the declared LHS (no type
// prefix needed). Unspecified fields are zero. Empty map literal still works
// (struct inference only triggers when the declared type is a struct).
// Prints "ok <label>" / "FAIL <label>" then "INIT PASS".

import std.rawvec

struct Point { int x; int y }
struct Mixed { int n; bool flag; *int ptr }

fn check(bool c, string l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn main() {
    // POD struct zero-init via inferred {}
    Point p = {}
    check(p.x == 0 && p.y == 0, "Point {} zero-init")

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

    // empty map literal still resolves via declared map type (not hijacked)
    map(string, int) mp = {}
    mp.set("k", 42)
    int got = mp.get("k")
    check(got == 42, "map {} still works")

    print("INIT PASS")
}
