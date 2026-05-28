// memcheck_overhaul.ls — 内存模型整改（M-1 ~ M-5）汇总回归测试。
// 覆盖每个修复路径的极端场景，作为永久的内存安全回归守护。
// 目标：JIT + AOT 下 memcheck OK clean（0 leak / 0 dfree / 0 ifree）。
//
// 场景索引：
//   1. print 各种动态 string 参数            (M-1)
//   2. borrowed string 跨函数边界            (M-2 / BF-032)
//   3. enum ctor with borrowed string        (M-2 / BF-032)
//   4. struct ctor with string method        (M-4)
//   5. vec 元素 swap（index assign）         (M-4 / bug_23)
//   6. vec(string) push + pop + for          (BF-001)
//   7. map(string,string) set + get + read   (M-4 / BF-039)
//   8. match binder return                   (BF-029)
//   9. try 早返路径 string                   (BF-012)
//  10. 循环内 string 分配 + break            (BF-012)
//  11. 闭包捕获 string + int                 (Phase C/F)
//  12. 自递归 enum（Tree）recursive drop     (BF-015/023)
//  13. f-string with % literal               (今日修复)
//  14. vec(has_drop struct) index field 临时 (M-4.5)
//  15. map[key] string value 临时 / 转移      (BF-039)

struct Item {
    string name
    int qty
}

enum Box {
    Empty
    One(string)
    Pair(string, int)
}

enum Tree {
    Leaf
    Node(int value, Tree left, Tree right)
}

type IntFn = Block() -> int

// borrowed string 参数 → enum ctor（必须 clone）
fn wrap_enum(string s) -> Box {
    return One(s)
}

// borrowed string 参数 → struct ctor（必须 clone）
fn wrap_struct(string s) -> Item {
    return Item{name: s, qty: 1}
}

// borrowed string → 显式 copy
fn echo_copy(string s) -> string {
    return s.copy()
}

// match binder 作为返回值
fn unwrap_or(Box b, string fallback) -> string {
    match b {
        One(x) => { return x.copy() }
        Pair(x, n) => { return x.copy() }
        Empty => { return fallback.copy() }
    }
}

// try 早返：Result(int, string)
fn parse_pos(string s) -> Result(int, string) {
    if s.length == 0 {
        return Err("empty".upper())
    }
    return Ok(s.length)
}

fn use_try(string s) -> Result(int, string) {
    int n = try parse_pos(s)
    return Ok(n + 1)
}

// 自递归 enum 求和
fn tree_sum(Tree t) -> int {
    match t {
        Leaf => 0
        Node(v, l, r) => v + tree_sum(l) + tree_sum(r)
    }
}

fn main() -> int {
    // ===== 1. print 各种动态 string 参数 (M-1) =====
    string s = "hello"
    print(s.upper())                       // string 方法
    print("a" + "b")                       // 拼接
    int nn = 42
    print(f"n={nn}")                        // f-string
    print(echo_copy("xyz".upper()))         // 用户函数返回 string
    print("  HELLO  ".trim().lower())       // 链式方法
    print(s.upper(), s.lower(), "lit")      // 多参数

    // ===== 2. borrowed string 跨函数边界 (M-2) =====
    string owned = "world".upper()
    string e1 = echo_copy(owned)
    print(e1)
    print(owned)                            // owned 仍 live（借用，非 move）

    // ===== 3. enum ctor with borrowed string (M-2/BF-032) =====
    Box b1 = wrap_enum(owned)
    match b1 {
        One(v) => { print(v) }
        Pair(v, n) => { print(v) }
        Empty => { print("empty") }
    }
    print(owned)                            // 仍 live

    // ===== 4. struct ctor with string method (M-4) =====
    Item it = wrap_struct("name".upper())
    print(it.name)
    Item it2 = Item{name: "alice".upper(), qty: 30}
    print(it2.name)

    // ===== 5. vec 元素 swap via index assign (M-4/bug_23) =====
    vec(string) vs = ["AAA".copy(), "BBB".copy()]
    string a = vs[0]
    string b = vs[1]
    vs[0] = b
    vs[1] = a
    print(vs[0])
    print(vs[1])

    // ===== 6. vec(string) push + pop + for (BF-001) =====
    vec(string) names = []
    names.push("n1".upper())
    names.push("n2".upper())
    names.push("n3".upper())
    names.pop()
    for i in 0..names.length {
        print(names[i])
    }

    // ===== 7. map(string,string) set + get + read (M-4/BF-039) =====
    map(string, string) m = {}
    m.set("k1".upper(), "v1".upper())
    m.set("k2".upper(), "v2".upper())
    print(m["k1".upper()])                  // index 临时读取
    print(m.get("k2".upper()))              // get 临时读取
    string mv = m["k1".upper()]             // 转移给命名变量
    print(mv)

    // ===== 8. match binder return (BF-029) =====
    Box b2 = Pair("pair".upper(), 7)
    string r8 = unwrap_or(b2, "none".upper())
    print(r8)

    // ===== 9. try 早返路径 string (BF-012) =====
    Result(int, string) tr = use_try("abcd")
    match tr {
        Ok(v) => { print(f"try ok {v}") }
        Err(msg) => { print(msg) }
    }
    Result(int, string) tr2 = use_try("")   // 触发 Err 早返
    match tr2 {
        Ok(v) => { print(f"try ok {v}") }
        Err(msg) => { print(msg) }
    }

    // ===== 10. 循环内 string 分配 + break (BF-012) =====
    int k = 0
    while k < 5 {
        string loopstr = "iter".upper()
        if k == 2 {
            print(loopstr)
            break
        }
        k = k + 1
    }

    // ===== 11. 闭包捕获 string + int (Phase C/F) =====
    int base = 100
    string tag = "tag".upper()
    IntFn adder = || {
        return base + 1
    }
    print(f"closure={adder()}")
    print(tag)                              // tag 仍可用（未被闭包 move）

    // ===== 12. 自递归 enum Tree recursive drop (BF-015/023) =====
    Tree tree = Node(1, Node(2, Leaf, Leaf), Node(3, Leaf, Leaf))
    print(f"tree_sum={tree_sum(tree)}")

    // ===== 13. f-string with % literal (今日修复) =====
    int pct = 50
    print(f"{pct}% complete")

    // ===== 14. vec(has_drop struct) index field 临时 (M-4.5) =====
    vec(Item) vit = []
    vit.push(Item{name: "i1".upper(), qty: 1})
    vit.push(Item{name: "i2".upper(), qty: 2})
    print(vit[0].name)                      // 临时 struct clone，取字段后丢弃
    print(vit[1].qty)                       // 临时 struct，POD 字段
    Item taken = vit[0]                     // 所有权转移
    print(taken.name)

    // ===== 15. enum vec index 临时 (M-4.5 对照) =====
    vec(Box) vb = []
    vb.push(One("e0".upper()))
    vb.push(One("e1".upper()))
    match vb[0] {                           // 临时 enum clone
        One(x) => { print(x) }
        Pair(x, n) => { print(x) }
        Empty => { print("empty") }
    }
    Box bx = vb[1]                          // 所有权转移
    match bx {
        One(x) => { print(x) }
        Pair(x, n) => { print(x) }
        Empty => { print("empty") }
    }

    print("OVERHAUL DONE")
    return 0
}
