// Closure-foundation Phase A smoke: a closure captures another Block (by-clone)
// and calls it repeatedly. Verifies:
//   - the captured Block's env is deep-cloned into the capturing closure's env,
//   - the inner closure can be called more than once (call does not consume env),
//   - the source Block stays live after being captured (by-clone, not by-move),
//   - clone/drop balance (run with --memcheck for 0/0/0).
// Self-verifying: prints "A PASS" only if every check holds.

import std.core.str
import std.core.vec

type IntFn = Block(int) -> int
type NullFn = Block() -> int

def check(bool cond, int id) -> bool {
    if !cond {
        @print(id)
        @print("A FAIL")
    }
    return cond
}

def main() {
    bool ok = true

    // --- 1) capture a POD-env Block (its env is non-NULL: captures `bias`) ---
    int bias = 100
    IntFn sq = |x| x * x + bias          // env captures bias (POD by-copy)

    NullFn run = || {            // captures sq (Block, by-clone)
        int s = 0
        s = s + sq(2)                    // 4 + 100 = 104
        s = s + sq(3)                    // 9 + 100 = 109
        return s                         // 213
    }
    ok = check(run() == 213, 1) && ok
    ok = check(run() == 213, 2) && ok    // call again: env not consumed
    ok = check(sq(5) == 125, 3) && ok    // source Block still live after capture

    // --- 2) capture a has_drop-env Block (captures a Str by-move) ---
    Str pre = "abc"                      // len 3
    IntFn add_len = |x| x + pre.len()    // env owns Str (by-move); pre now moved

    NullFn run2 = || {           // captures add_len (Block, by-clone)
        int s = 0
        s = s + add_len(10)              // 13
        s = s + add_len(20)              // 23
        return s                         // 36
    }
    ok = check(run2() == 36, 4) && ok
    ok = check(run2() == 36, 5) && ok    // independent clone, callable repeatedly
    ok = check(add_len(0) == 3, 6) && ok // source still live (env deep-cloned)

    // --- 3) an outer closure that CAPTURES a Block, stored in a Vec then
    //        copied out: exercises env_clone's Block branch — the nested
    //        captured Block must deep-clone one more layer so the element and
    //        the copy each own an independent inner env. ---
    int delta = 7
    IntFn addd = |x| x + delta           // POD-env Block
    Vec(NullFn) box = {}
    box.push(|| { return addd(100) + addd(200) })   // captures addd by-clone
    NullFn copied = box[0]               // copy-out → env_clone deep-copies addd
    ok = check(copied() == 314, 7) && ok // 107 + 207
    ok = check(box[0]() == 314, 8) && ok // original element still valid
    ok = check(addd(0) == 7, 9) && ok    // source addd still live

    if ok { @print("A PASS") }
}
