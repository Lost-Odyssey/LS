// trait_self_test.ls — Step 10: Self keyword in trait signatures and impl bodies

trait Addable {
    fn add(&self, f64 dx, f64 dy) -> Self
}

struct Vec2 {
    f64 x
    f64 y
}

impl Addable for Vec2 {
    fn add(&self, f64 dx, f64 dy) -> Vec2 {
        return Vec2 { x: self.x + dx, y: self.y + dy }
    }
}

// Self as return type in non-trait impl
struct Point {
    int x
    int y
}

impl Point {
    fn translate(&self, int dx, int dy) -> Self {
        return Point { x: self.x + dx, y: self.y + dy }
    }
}

// Constrained generic using Addable
fn shift(T: Addable)(T a, f64 dx, f64 dy) -> T {
    return a.add(dx, dy)
}

fn main() {
    Vec2 a = Vec2 { x: 1.0, y: 2.0 }

    // Direct trait method call
    Vec2 c = a.add(3.0, 4.0)
    print(c.x)
    print(c.y)

    // Via constrained generic
    Vec2 a2 = Vec2 { x: 10.0, y: 20.0 }
    Vec2 d = shift(Vec2)(a2, 5.0, 6.0)
    print(d.x)
    print(d.y)

    // Self in non-trait impl
    Point p = Point { x: 1, y: 2 }
    Point q = p.translate(10, 20)
    print(q.x)
    print(q.y)
}
