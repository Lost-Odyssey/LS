// block_mutref_test.ls — Block(&!T) 可写借用块参数（plan_std_map §13 最后一项）
// 旧 bug：调用点 codegen_block_call 的 TYPE_REFERENCE 分支不识别 AST_MUT_BORROW
// 实参，把指针当值物化进 pointee 大小临时 → 闭包对垃圾内存写 → 堆损坏(0xC0000374)。
import std.vec
import std.map
import std.str

type VecGrower = Block(&!Vec(int))
type StrUpd    = Block(&!Str)
type MapUpd    = Block(&!Map(Str, int))
type VecAddN   = Block(int, &!Vec(int))
type VecReader = Block(&Vec(int)) -> int

fn apply_grow(&!Vec(int) v, VecGrower f) {
    f(&!v)    // 转发：对已是 &! 借用的参数再取 &!
}

fn check(bool cond, Str label) {
    if (cond) { print(f"PASS {label}") } else { print(f"FAIL {label}") }
}

fn main() -> int {
    // ---- 直接调用：闭包内 push 对调用方可见 ----
    Vec(int) v = [1, 2]
    VecGrower grow = |x| { x.push(99) }
    grow(&!v)
    check(v.len() == 3 && v[2] == 99, "direct-mut-borrow-vec")

    // ---- 经函数转发（&!参数 再 &!）----
    apply_grow(&!v, grow)
    check(v.len() == 4, "forwarded-mut-borrow")

    // ---- 多次调用同一闭包 ----
    grow(&!v)
    grow(&!v)
    check(v.len() == 6, "repeat-calls")

    // ---- 混合参数（POD + &!Vec）----
    VecAddN addn = |n, x| { x.push(n) }
    addn(7, &!v)
    check(v.len() == 7 && v[6] == 7, "mixed-pod-and-mut-borrow")

    // ---- &!Str 载体 ----
    Str s = "ab"
    StrUpd upd = |t| { t.push_str("cd") }
    upd(&!s)
    check(s.eq?("abcd"), "mut-borrow-str")

    // ---- &!Map 载体 ----
    Map(Str, int) m = {"a": 1}
    MapUpd mupd = |mm| { mm.set("b", 2) }
    mupd(&!m)
    check(m.len() == 2 && m["b"] == 2, "mut-borrow-map")

    // ---- 只读 &T 块参数：显式 &v 与裸 v 同路（§13 ② 剥壳） ----
    VecReader rd = |x| { return x.len() }
    check(rd(&v) == 7 && rd(v) == 7, "readonly-explicit-and-auto")

    // ---- 借用调用后源全部存活 ----
    check(v.len() == 7 && s.len() == 4 && m.len() == 2, "sources-alive")

    print("BLOCK_MUTREF PASS")
    return 0
}
