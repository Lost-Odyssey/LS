// Borrowed slices cannot escape via return — a `&array(T)` carries a borrowed *T into
// a local Vec, so returning it would dangle. Must be a clean compile-time reject.
import std.core.vec

def bad() -> &array(int) {
    Vec(int) v = [1, 2, 3]
    return v[0..2]              // slice of a local → would dangle
}

def main() -> int {
    @print(1)
    return 0
}
