// Closure-foundation Phase B — nested closure literals (transitive capture).
//
// A closure body may now contain another closure literal `|x| ...`. Variables
// referenced by an inner closure that live BEYOND the enclosing closure (in
// the function scope) are "transitive": the enclosing closure must capture them
// too (threaded through its env) so the inner closure can resolve them from the
// enclosing closure's body. v1 allows transitive POD (by-copy) and Block
// (by-clone); transitive by-move (Str / has_drop) is rejected (see
// nested_closure_reject.ls). by-move of an enclosing closure's OWN local into an
// inner closure is one level — allowed (case 5 below).
//
// Self-verifying: prints "NC PASS" only if every check holds. Run under
// --memcheck for 0/0/0 to prove the env clone/drop balance across layers.

import std.str

type IntFn  = Block(int) -> int
type NullFn = Block() -> int

fn check(bool cond, int id) -> bool {
    if !cond { print(id) print("NC FAIL") }
    return cond
}

fn main() {
    bool ok = true

    // 1) inner refs the enclosing closure's own local (not transitive) AND a
    //    function-level POD var (transitive by-copy into the enclosing closure).
    int base = 1000
    NullFn outer1 = || {
        int local = 50
        IntFn inner = |x| x + local + base   // local: from O ; base: transitive
        return inner(5) + inner(6)           // (5+50+1000)+(6+50+1000)=2111
    }
    ok = check(outer1() == 2111, 1) && ok

    // 2) transitive Block (by-clone): a function-scope Block referenced from an
    //    inner closure is captured by the enclosing closure by-clone, then by the
    //    inner closure by-clone again. The source Block stays live.
    int bias = 100
    IntFn sq = |x| x * x + bias              // function-scope Block (POD env)
    NullFn outer2 = || {
        IntFn use = |y| sq(y) + 1            // sq: transitive by-clone
        return use(3) + use(4)               // (9+100+1) + (16+100+1) = 227
    }
    ok = check(outer2() == 227, 2) && ok
    ok = check(sq(5) == 125, 3) && ok        // source Block still live

    // 3) two-level nesting: w is transitive through O, then I, then J.
    int w = 7
    NullFn outer3 = || {
        NullFn mid = || {
            IntFn deep = |z| z + w           // w: O <- I <- J
            return deep(10) + deep(20)       // 17 + 27 = 44
        }
        return mid()
    }
    ok = check(outer3() == 44, 4) && ok

    // 4) the enclosing closure also calls the captured-into transitive Block
    //    directly (mixed direct + nested use of the same by-clone Block).
    IntFn dbl = |x| x + x
    NullFn outer4 = || {
        IntFn via = |y| dbl(y)               // dbl: transitive by-clone
        return dbl(3) + via(4)               // 6 + 8 = 14
    }
    ok = check(outer4() == 14, 5) && ok

    // 5) by-move of the enclosing closure's OWN local (a Str) into an inner
    //    closure is ONE level — allowed (the inner closure by-moves it from O,
    //    not across a function boundary). Exercises has_drop env in a layer.
    NullFn outer5 = || {
        Str s = "abcde"                      // O-local Str (has_drop)
        NullFn inner = || s.len()            // by-move s from O into inner
        return inner()                       // 5
    }
    ok = check(outer5() == 5, 6) && ok

    if ok { print("NC PASS") }
}
