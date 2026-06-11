// Phase A.5 验证：每种主要 alloc kind 都能被精确归类。
// 期望 0 leak —— 这里只是 exercise 各路径，确认 kind 标签生效。
fn main() -> int {
    Str a = "hello".upper()       // Str.upper
    Str b = "WORLD".lower()       // Str.lower
    Str c = "  pad  ".trim()      // Str.trim
    Str d = "abcdef".substr(1, 3) // Str.substr
    Str e = "x".copy()            // Str.copy
    Str f = a + b                  // Str.concat
    Str g = f"got {f}"             // Str.fstring

    print(a)
    print(b)
    print(c)
    print(d)
    print(e)
    print(f)
    print(g)
    return 0
}
