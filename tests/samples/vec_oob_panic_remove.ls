// negative: remove at out-of-range index must abort (i >= len)
import std.vec
fn main() -> int {
    Vec(int) v = {}
    for i in 0..3 { v.push(i) }
    int x = v.remove(10)    // out of range -> abort
    print("AFTER")          // must NOT run
    return 0
}
