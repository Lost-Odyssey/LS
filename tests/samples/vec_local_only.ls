// vec_local_only_test.ls — local vec only, no globals
import std.vec

fn main() -> int {
    Vec(int) v = {}
    v.push(10)
    v.push(20)
    v.push(30)
    print(v.len())     // 3
    print(v[0])        // 10
    print(v[1])        // 20
    print(v[2])        // 30
    int s = 0
    for (int i = 0; i < v.len(); i = i + 1) {
        int x = v[i]
        s = s + x
    }
    print(s)           // 60
    return 0
}
