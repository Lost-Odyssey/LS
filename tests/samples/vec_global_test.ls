// vec_global_test.ls — global vec(T) test

vec(int) g_nums

fn add_nums() {
    g_nums.push(1)
    g_nums.push(2)
    g_nums.push(3)
}

fn main() -> int {
    add_nums()
    print(g_nums.length)   // 3
    print(g_nums[0])       // 1
    print(g_nums[1])       // 2
    print(g_nums[2])       // 3
    int s = 0
    for x in g_nums { s = s + x }
    print(s)               // 6
    return 0
}
