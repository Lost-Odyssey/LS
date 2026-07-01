// BF: `Type name = a * b` (bare ident * ident in declaration initializer)
// previously misparsed as a pointer declaration ("unknown type 'b'").

struct Vec2 { f64 x; f64 y }

methods Vec2: Mul { def *(&self, &Vec2 rhs) -> Vec2 { return Vec2{ x: self.x * rhs.x, y: self.y * rhs.y } } }

def main() {
    int a = 6
    int b = 7
    int e = a * b          // <- the bug: must parse as multiplication
    @print(e)               // 42

    f64 c = 2.5
    f64 d = 4.0
    f64 g = c * d
    @print(g)               // 10

    // chained, no parens
    int h = a * b * 2
    @print(h)               // 84

    // operator-overload form: `Vec2 r = a * b` without parens
    Vec2 p = Vec2{ x: 2.0, y: 3.0 }
    Vec2 q = Vec2{ x: 4.0, y: 5.0 }
    Vec2 r = p * q
    @print(r.x)             // 8
    @print(r.y)             // 15

    @print("DECL_MUL PASS")
}
