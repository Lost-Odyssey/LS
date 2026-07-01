// negative: truncate to a negative length must abort
import std.core.vec
def main() -> int {
    Vec(int) v = {}
    for i in 0..3 { v.push(i) }
    v.truncate(0 - 1)       // negative length -> abort
    @print("AFTER")          // must NOT run
    return 0
}
