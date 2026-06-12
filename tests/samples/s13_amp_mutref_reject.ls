// 负向：显式 `&x` 不可冒充可写借用 `&!T` —— 必须仍是编译错误
import std.vec

fn grow(&!Vec(int) v) {
    v.push(99)
}

fn main() -> int {
    Vec(int) v = [1]
    grow(&v)    // 错：可写借用须显式 &!v
    return 0
}
