// M-5 负向：v[i] = s 是 move，move 后使用源 → 编译期拒绝。
// 期望：[move error] ... use of moved variable 's'，rc != 0。
fn main() -> int {
    vec(string) v = ["a".copy(), "b".copy()]
    string s = "x".upper()
    v[0] = s
    print(s)        // ← s 已 move 进 v[0]，编译错误
    return 0
}
