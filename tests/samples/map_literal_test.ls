// map_literal_test.ls — std.map M-LIT: `{ k: v, ... }` key-value literals
// (frontend §F1). `:` pairs build AST_MAP_LIT, routed by LHS type to the
// __from_pairs protocol (keys/values moved in). Covers POD/string/Vec values,
// empty `{}`, trailing comma, and confirms anonymous struct literals (IDENT-keyed
// `:`) still parse as structs. See docs/plan_std_map.md §F1/§7.2. JIT+AOT+memcheck.

import std.map
import std.vec
import std.str

fn check(bool c, Str l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

struct Point { int x; int y }

fn gi(&Map(Str, int) m, Str k) -> int {
    match m.get(k) { Some(v) => { return v } None => { return -1 } }
}

fn main() {
    // string keys, int values
    Map(Str, int) m = { "a": 1, "b": 2, "c": 3 }
    check(m.len() == 3, "literal len 3")
    check(gi(m, "a") == 1 && gi(m, "b") == 2 && gi(m, "c") == 3, "literal values")
    check(gi(m, "z") == -1, "literal miss")

    // int keys
    Map(int, int) im = { 10: 100, 20: 200, 30: 300 }
    check(im.len() == 3, "int-key literal len")

    // empty literal (M-DEF) + trailing comma + owned-temp value
    Map(Str, int) e = {}
    check(e.empty?(), "empty literal")
    int x = 7
    Map(Str, int) tc = { "k": x + 1, }
    check(gi(tc, "k") == 8, "owned-temp value + trailing comma")

    // string values (owned move-in)
    Map(Str, Str) ss = { "name": "alice", "city": "paris" }
    int nlen = 0
    match ss.get("name") { Some(s) => { nlen = s.len() } None => {} }
    check(nlen == 5, "string-value literal")

    // nested Vec values via [..]
    Map(Str, Vec(int)) mv = { "a": [1, 2, 3], "b": [4, 5] }
    int sz = 0
    match mv.get("a") { Some(v) => { sz = v.len() } None => {} }
    check(sz == 3 && mv.len() == 2, "nested Vec value literal")

    // anonymous struct literal (IDENT-keyed `:`) must still parse as a struct
    Point p = { x: 3, y: 4 }
    check(p.x == 3 && p.y == 4, "anonymous struct literal intact")

    print("MLIT PASS")
}
