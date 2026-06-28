// negative: insert at out-of-range index must abort (i > len)
import std.core.vec
def main() -> int {
    Vec(int) v = {}
    for i in 0..3 { v.push(i) }
    v.insert(10, 99)        // out of range -> abort
    @print("AFTER")          // must NOT run
    return 0
}
