// Phase A memcheck smoke test.
// 期望: 跑 `ls run --memcheck` 末尾输出 [memcheck] OK clean
fn main() -> int {
    // 1. 字符串方法返 owned string
    string a = "hello".upper()
    print(a)

    // 2. f-string 格式化（产 owned）
    int n = 42
    string b = f"answer={n}"
    print(b)

    // 3. 字符串拼接（产 owned 中间值）
    string c = "abc" + "def"
    print(c)

    // 4. lower / trim / substr
    string d = "  WORLD  ".trim().lower()
    print(d)

    return 0
}
