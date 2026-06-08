import std.vec

/* Phase 5 negative: __move() on a borrowed string is forbidden. */

Vec(string) g_bucket

fn stash(&string s) {
    g_bucket.push(__move(s))
}

fn main() -> int {
    string name = "Alice".upper()
    stash(name)
    return 0
}
