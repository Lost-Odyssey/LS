// Storing through a read-only slice `&array(T)` must be rejected — only a
// writable view `&!array(T)` permits element assignment.
import std.core.vec

def main() -> int {
    Vec(int) v = [1, 2, 3]
    &array(int) s = v[0..2]
    s[0] = 9                   // read-only slice → must reject
    return 0
}
