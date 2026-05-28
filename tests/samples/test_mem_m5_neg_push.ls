// M-5 负向：vec.push 是 move，move 后使用源 → 编译期拒绝。
// 期望：[move error] ... use of moved variable 's'，rc != 0。
fn main() -> int {
    string s = "hello".upper()
    vec(string) v = []
    v.push(s)
    print(s)        // ← s 已 move 进 v，编译错误
    return 0
}
