// 负向①：只读 &T 闭包参数体内调 &!self 方法应拒
import std.core.vec

type Reader = Block(&Vec(int))

def main() -> int {
    Vec(int) v = [1]
    Reader f = |x| { x.push(99) }
    f(&v)
    return 0
}
