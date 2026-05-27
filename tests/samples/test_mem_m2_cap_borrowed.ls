// M-2 回归测试：borrowed string 参数（cap=-2）在各消费站点的正确性
// 验证 cap=0(static) 与 cap=-2(borrowed) 正确区分后，不再出现 dangling / leak / dfree
//
// 用法：
//   ls run tests/samples/test_mem_m2_cap_borrowed.ls
//   ls run --memcheck tests/samples/test_mem_m2_cap_borrowed.ls
// 期望：两者均输出正确结果，memcheck 报告 OK clean（0 leak, 0 double-free）

// ---- 辅助 enum / struct ----

enum Wrapper {
    Val(string)
    Empty
}

struct Box {
    string content
}

// ---- 函数：把 borrowed string 存入 enum ----
fn wrap_enum(string s) -> Wrapper {
    return Val(s)        // s is borrowed; must clone into Val payload
}

// ---- 函数：把 borrowed string 存入 struct ----
fn wrap_struct(string s) -> Box {
    return Box{content: s}   // s is borrowed; must clone into Box.content
}

// ---- 函数：把 borrowed string 拼接后返回 owned ----
fn append_suffix(string s) -> string {
    return s + "_OK"
}

// ---- 函数：调用 string 方法后返回 ----
fn to_upper_copy(string s) -> string {
    return s.upper()
}

// ---- 函数：f-string 含 borrowed param ----
fn fmt_it(string s) -> string {
    return f"[{s}]"
}

fn main() -> int {
    string owned = "hello"

    // Case 1: wrap_enum — borrowed param stored into enum Val(string)
    Wrapper w = wrap_enum(owned)
    match w {
        Val(v) => print(v)
        Empty  => print("empty")
    }

    // Case 2: wrap_struct — borrowed param stored into struct field
    Box b = wrap_struct(owned)
    print(b.content)

    // Case 3: append_suffix — borrowed param used in concat
    string appended = append_suffix(owned)
    print(appended)

    // Case 4: to_upper_copy — borrowed param used in method call
    string upper = to_upper_copy(owned)
    print(upper)

    // Case 5: fmt_it — borrowed param used in f-string
    string formatted = fmt_it(owned)
    print(formatted)

    // Case 6: owned is still alive (borrow didn't consume it)
    print(owned)

    // Case 7: static literal passed directly — still cap=0, no clone needed
    Wrapper ws = wrap_enum("literal")
    match ws {
        Val(v) => print(v)
        Empty  => print("empty")
    }

    // Case 8: chained — result of method call (owned) wrapped into enum
    string chained = "world".upper()
    Wrapper wc = wrap_enum(chained)
    match wc {
        Val(v) => print(v)
        Empty  => print("empty")
    }

    print("ALL PASS")
    return 0
}
