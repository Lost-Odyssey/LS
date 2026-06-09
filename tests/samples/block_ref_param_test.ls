// block_ref_param_test.ls — regression for B-MAP-M5-002: a Block/closure
// parameter of reference type `&T` (e.g. Block(&Map(K,V))->R, Block(&Struct)->R)
// must use the pointer borrow ABI end-to-end:
//   * checker: unwrap `&T`→T (is_borrow) for the closure body so `pp.field` /
//     `pp.method()` type-check;
//   * codegen call site: pass the pointer (not the loaded value);
//   * codegen closure body: register the param as the incoming pointer (borrow).
// Previously failed checker ("field access on non-struct type '&T'") then LLVM
// verifier ("Call parameter type does not match" — Map passed by value to ptr).
// See docs/plan_std_map.md §13 B-MAP-M5-002. JIT + AOT + memcheck 0/0/0.

import std.map
import std.vec

fn check(bool c, string l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

struct P { int x; int y }

fn apply_p(&P p, Block(&P) -> int q) -> int { return q(p) }
fn apply_m(&Map(string, int) m, Block(&Map(string, int)) -> int q) -> int { return q(m) }

fn main() {
    // &struct block param: field access
    P p = P{ x: 3, y: 4 }
    check(apply_p(p, |pp| { return pp.x + pp.y }) == 7, "Block(&P) field access")

    // &Map block param: method call; source stays usable after (borrow)
    Map(string, int) m = {}
    m.set("a", 10)
    m.set("b", 20)
    check(apply_m(m, |mm| { return mm.len() }) == 2, "Block(&Map) len")
    int got = apply_m(m, |mm| {
        match mm.get("b") { Some(v) => { return v } None => { return -1 } }
    })
    check(got == 20, "Block(&Map) get")
    check(m.len() == 2, "source Map still usable after borrow-block calls")

    // multiple invocations of the same block over a borrowed Map
    int total = 0
    int i = 0
    while i < 3 {
        total = total + apply_m(m, |mm| { return mm.len() })
        i = i + 1
    }
    check(total == 6, "repeated Block(&Map) calls")

    print("BLOCKREF PASS")
}
