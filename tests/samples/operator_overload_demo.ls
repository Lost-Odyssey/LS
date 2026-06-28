// operator_overload_demo.ls — operator overloading end-to-end test

import std.core.vec

struct Vec2 { f64 x; f64 y }

methods Vec2: Add { def +(&self, &Vec2 rhs) -> Vec2 { return Vec2{ x: self.x + rhs.x, y: self.y + rhs.y } } }
methods Vec2: Sub { def -(&self, &Vec2 rhs) -> Vec2 { return Vec2{ x: self.x - rhs.x, y: self.y - rhs.y } } }
methods Vec2: Mul { def *(&self, &Vec2 rhs) -> Vec2 { return Vec2{ x: self.x * rhs.x, y: self.y * rhs.y } } }

methods Vec2: Equal {
    def ==(&self, &Vec2 rhs) -> bool { return self.x == rhs.x && self.y == rhs.y }
}

methods Vec2: Order {
    def <(&self, &Vec2 rhs) -> bool {
        f64 a = self.x * self.x + self.y * self.y
        f64 b = rhs.x * rhs.x + rhs.y * rhs.y
        return a < b
    }
}

// Generic function constrained on Add — operator usable in generic body.
def sum_all(T: Add)(Vec(T) xs, T zero) -> T {
    T acc = zero
    for x in xs {
        acc = acc + x
    }
    return acc
}

def main() {
    Vec2 a = Vec2{ x: 1.0, y: 2.0 }
    Vec2 b = Vec2{ x: 3.0, y: 4.0 }

    Vec2 c = a + b
    Vec2 d = b - a
    Vec2 e = a * b
    @print(c.x); @print(c.y)   // 4 6
    @print(d.x); @print(d.y)   // 2 2
    @print(e.x); @print(e.y)   // 3 8

    // equality + derived !=
    @print(a == b)   // 0 (false)
    @print(a != b)   // 1 (true)  derived from ==
    @print(a == a)   // 1 (true)

    // ordering: only `<` defined, > <= >= derived
    @print(a < b)    // 1 (true)
    @print(a > b)    // 0 (false) derived
    @print(b > a)    // 1 (true)  derived
    @print(a <= a)   // 1 (true)  derived
    @print(a >= b)   // 0 (false) derived

    // generic sum over Vec(Vec2)
    Vec(Vec2) vs = [a, b, c]
    Vec2 total = sum_all(Vec2)(vs, Vec2{ x: 0.0, y: 0.0 })
    @print(total.x)  // 8
    @print(total.y)  // 12
}
