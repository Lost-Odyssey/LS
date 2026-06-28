/* Test Vec index read v[i] (bounds-checked; get now returns Option(T)). */
import std.core.vec

def main() -> int {
    Vec(Str) v = {}
    v.push("a")
    v.push("b")
    v.push("c")
    @print(v[0])     /* a */
    @print(v[1])     /* b */
    @print(v[2])     /* c */

    Vec(int) ints = {}
    ints.push(10)
    ints.push(20)
    ints.push(30)
    @print(ints[0])  /* 10 */
    @print(ints[2])  /* 30 */
    return 0
}
