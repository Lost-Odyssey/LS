// de-Rust 关键字 smoke：def / methods / interface / private + 合并 trait-impl
// 形态 `methods Type: Interface`（含泛型 methods(T)）。退役后旧 fn/impl/trait/
// pub/priv 应报错（见 derust_keywords_reject.ls）。
interface Greet {
    def hello(&self) -> int
}

struct Box {
    private int secret
}

methods Box {
    static def make(int s) -> Box { return Box { secret: s } }
    def peek(&self) -> int { return self.secret }
}

// 合并形态（类型在前 + 冒号），等价于旧 `impl Greet for Box`
methods Box: Greet {
    def hello(&self) -> int { return self.peek() + 1 }
}

// 泛型 struct + 固有方法块
struct Pair(T) {
    T a
    T b
}

methods(T) Pair(T) {
    def first(&self) -> T { return self.a }
}

def main() -> int {
    Box b = Box.make(41)
    Pair(int) p = Pair { a: 7, b: 9 }
    if b.peek() == 41 && b.hello() == 42 && p.first() == 7 {
        @print("DERUST OK")
    } else {
        @print("DERUST FAIL")
    }
    return 0
}
