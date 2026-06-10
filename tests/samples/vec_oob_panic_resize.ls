// negative: resize to a negative length must abort
import std.vec
fn main() -> int {
    Vec(int) v = {}
    for i in 0..3 { v.push(i) }
    v.resize(0 - 1, 7)      // negative length -> abort
    print("AFTER")          // must NOT run
    return 0
}
