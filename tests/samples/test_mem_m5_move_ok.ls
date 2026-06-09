// M-5 正向：checker move 分析与 codegen 所有权语义一致性回归。
// 验证 checker 既不误报（clone 语义后源仍可用），也不漏报内存错误。
// 目标：编译通过 + memcheck OK clean（0 leak / 0 dfree / 0 ifree）。
// 负向（move-after-use 编译期拒绝）见 test_mem_m5_neg_*.ls。

import std.vec
import std.map

struct P { string name }
enum E { V(string) N }

fn take_str(string s) -> int {     // by-value：move 形参
    print(s)
    return 1
}

fn main() -> int {
    // ===== move 语义：move 后不再使用源（合法） =====
    // vec.push 是 move
    string a = "aaa".upper()
    Vec(string) v = {}
    v.push(a)                       // a 被 move 进 v
    print(v[0])

    // v[i] = s 是 move
    string b = "bbb".upper()
    v[0] = b                        // b 被 move
    print(v[0])

    // map.set value 是 move
    string c = "ccc".upper()
    Map(string, string) m = {}
    m.set("k".upper(), c)           // c 被 move 进 m
    match m.get("k".upper()) {
        Some(mc) => { print(mc) }
        None => { print("missing") }
    }

    // 函数实参 by-value 是 move
    string d = "ddd".upper()
    int n = take_str(d)             // d 被 move 给形参
    print(n)

    // ===== clone 语义：ctor 后源仍 live（合法继续使用） =====
    // enum ctor payload string 是 clone
    string e = "eee".upper()
    E ev = V(e)                     // e 被 clone 进 enum
    print(e)                        // e 仍 live
    match ev {
        V(x) => { print(x) }
        N => { print("none") }
    }

    // struct ctor field string 是 clone
    string f = "fff".upper()
    P p = P{name: f}                // f 被 clone 进 struct
    print(f)                        // f 仍 live
    print(p.name)

    // ===== 分支：仅一条路径 move → 之后不可用（此处只走合法路径） =====
    // 两条路径都 move，分支后不再使用（合法）
    string g = "ggg".upper()
    if n > 0 {
        v.push(g)                   // then 分支 move g
    } else {
        print(g)                    // else 分支只读（也消耗，分支后 g 不再用）
    }
    print(v[1])

    print("done")
    return 0
}
