// 负向②：裸 v 传 Block(&!T) 参数应拒（可写借用必须显式 &!）
import std.vec

type Grower = Block(&!Vec(int))

fn main() -> int {
    Vec(int) v = [1]
    Grower f = |x| { x.push(99) }
    f(v)
    return 0
}
