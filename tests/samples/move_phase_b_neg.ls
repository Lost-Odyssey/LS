import std.vec

// Expected to fail type checking: Phase B should reject use of a MAYBE_MOVED
// variable on ANY path after an if-without-else that moves it.
fn main() -> int {
    Vec(Str) v = {}
    Str s = "hello".upper()
    bool c = true
    if c {
        v.push(s)    // s moved only in this branch -> MAYBE_MOVED after the if
    }
    print(s)         // [move error] use of maybe-moved variable 's'
    return 0
}
