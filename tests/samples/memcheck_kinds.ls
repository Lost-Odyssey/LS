// Phase A.5 验证：每种主要 alloc kind 都能被精确归类。
// 期望 0 leak —— 这里只是 exercise 各路径，确认 kind 标签生效。
fn main() -> int {
    string a = "hello".upper()       // string.upper
    string b = "WORLD".lower()       // string.lower
    string c = "  pad  ".trim()      // string.trim
    string d = "abcdef".substr(1, 3) // string.substr
    string e = "x".copy()            // string.copy
    string f = a + b                  // string.concat
    string g = f"got {f}"             // string.fstring

    print(a)
    print(b)
    print(c)
    print(d)
    print(e)
    print(f)
    print(g)
    return 0
}
