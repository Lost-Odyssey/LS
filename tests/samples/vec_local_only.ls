// vec_local_only_test.ls — local vec only, no globals

fn main() -> int {
    vec(int) v
    v.push(10)
    v.push(20)
    v.push(30)
    print(v.length)    // 3
    print(v[0])        // 10
    print(v[1])        // 20
    print(v[2])        // 30
    int s = 0
    for x in v { s = s + x }
    print(s)           // 60
    return 0
}
