// vec_global_test.ls — global vec(T) test
import std.vec

Vec(int) g_nums = {}

fn add_nums() {
    g_nums.push(1)
    g_nums.push(2)
    g_nums.push(3)
}

fn main() -> int {
    add_nums()
    print(g_nums.len())    // 3
    print(g_nums[0])       // 1
    print(g_nums[1])       // 2
    print(g_nums[2])       // 3
    int s = 0
    for (int i = 0; i < g_nums.len(); i = i + 1) {
        int x = g_nums[i]
        s = s + x
    }
    print(s)               // 6
    g_nums.clear()
    g_nums.shrink_to_fit()
    return 0
}
