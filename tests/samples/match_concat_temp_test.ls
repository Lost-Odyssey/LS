// A-4 (docs/bugs_deferred_p5_4.md §4): a chained-operator receiver spill
// (`"lit" + s + "lit"`) inside a match-arm `if` body registered a has_drop temp
// whose drop was reached on the fall-through path that never initialized it →
// free of stack garbage → non-deterministic crash (rc 0/127). The intermediate
// concat temp's spill alloca is now zero-initialized in the entry block, so a
// stray drop is a safe no-op.
//
// Self-verifying: 3 consecutive `match Result(Str,Str)` blocks, each with a
// 3-part concat in the NON-taken arm. Prints "MATCHCAT PASS" on success.

import std.str

fn gen(int n) -> Result(Str, Str) {
    if n == 0 { return Ok("hello") }
    if n == 1 { return Ok("world") }
    return Ok("42")
}

fn run() {
    match gen(0) {
        Ok(s) => { if s != "hello" { print("FAIL1 '" + s + "'") return } }
        Err(e) => { print("FAIL: err1 " + e); return }
    }
    match gen(1) {
        Ok(s) => { if s != "world" { print("FAIL2 '" + s + "'") return } }
        Err(e) => { print("FAIL: err2 " + e); return }
    }
    match gen(2) {
        Ok(s) => { if s != "42" { print("FAIL3 '" + s + "'") return } }
        Err(e) => { print("FAIL: err3 " + e); return }
    }
    print("MATCHCAT PASS")
}

fn main() -> int {
    run()
    return 0
}
