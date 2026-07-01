// Negative: comptime match requires an ENUM subject. A struct subject is a clean
// compile error (not a crash, not silently empty).

struct P { int x; int y }

def bad(T)(&T v) -> int {
    comptime match v {
        vr(p) => { return 0 }
    }
}

def main() -> int {
    P p = P { x: 1, y: 2 }
    @print(bad(&p))       // <-- P is a struct, not an enum
    return 0
}
