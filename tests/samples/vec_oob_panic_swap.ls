// negative: swap with an out-of-range index must abort
import std.vec
fn main() -> int {
    Vec(int) v = {}
    for i in 0..3 { v.push(i) }
    v.swap(0, 99)           // out of range -> abort
    print("AFTER")          // must NOT run
    return 0
}
