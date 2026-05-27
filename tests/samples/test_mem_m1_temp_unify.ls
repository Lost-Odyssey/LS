// M-1 回归测试：print 参数中各种动态 string 来源不 leak 不 double-free
// 验证统一 temp_string_slots 机制（消除 __argtmp 双轨后）的正确性
// 用法：ls run --memcheck tests/samples/test_mem_m1_temp_unify.ls
//       两者均须输出 [memcheck] OK clean

fn make_str() -> string {
    return "dynamic".upper()
}

fn make_concat() -> string {
    return "hello" + " world"
}

fn main() -> int {
    string s = "hello"

    // Case 1: string 方法直接作为 print 参数
    print(s.upper())

    // Case 2: 链式方法
    print("  HELLO  ".trim().lower())

    // Case 3: string 拼接
    print("aaa" + "bbb")

    // Case 4: 用户函数返回 string
    print(make_str())

    // Case 5: 用户函数返回 string（拼接版）
    print(make_concat())

    // Case 6: 多参数，含动态 string
    print(s.upper(), s.lower())

    // Case 7: substr 方法
    print("hello world".substr(0, 5))

    // Case 8: replace 方法
    print("hello world".replace("world", "LS"))

    // Case 9: f-string 内含方法调用（inline 展开路径）
    int n = 42
    print(f"n={n} upper={s.upper()}")

    // Case 10: 嵌套方法链
    print("Hello World".lower().upper())

    print("ALL PASS")
    return 0
}
