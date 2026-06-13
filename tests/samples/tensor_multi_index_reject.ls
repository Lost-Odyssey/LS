// tensor_multi_index_reject.ls — 负向：对没有 __index{N} 协议的类型多下标应编译期拒绝。
// Vec(int) 只有单下标 __index，没有 __index2 → `v[i,j]` 须报错（不是静默/崩溃）。
import std.str
import std.vec

fn main() {
    Vec(int) v = [1, 2, 3, 4]
    int x = v[0, 1]          // 2-D index on a 1-D Vec — no __index2 → compile error
    print(f"{x}")
}
