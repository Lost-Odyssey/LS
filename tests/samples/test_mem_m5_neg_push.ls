// M-5 负向：变量绑定 move 后使用源 → 编译期拒绝。
// 期望：[move error] ... use of moved variable 's'，rc != 0。
fn main() -> int {
    string s = "hello".upper()
    string b = s
    print(s)        // ← s 已 move 到 b，编译错误
    return 0
}
