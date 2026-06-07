/* Test Vec.get(i). Vec.get does NOT bounds-check (VR-LIM-003). */
import std.vec

fn main() -> int {
    Vec(string) v = {}
    v.push("a")
    v.push("b")
    v.push("c")
    print(v.get(0))     /* a */
    print(v.get(1))     /* b */
    print(v.get(2))     /* c */

    Vec(int) ints = {}
    ints.push(10)
    ints.push(20)
    ints.push(30)
    print(ints.get(0))  /* 10 */
    print(ints.get(2))  /* 30 */
    return 0
}
