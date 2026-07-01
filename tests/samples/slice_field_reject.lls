// A slice field would dangle (it stores a borrowed *T whose Vec may drop first).
// Storing a borrowed view must be a clean compile-time reject.
import std.core.vec

struct Holder { &array(int) s }

def main() -> int {
    @print(1)
    return 0
}
