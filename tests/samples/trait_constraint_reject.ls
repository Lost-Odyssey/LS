// trait_constraint_reject.ls — negative: type doesn't satisfy trait bound

trait Describable {
    fn describe(&self) -> Str
}

struct Circle {
    int radius
}

impl Describable for Circle {
    fn describe(&self) -> Str {
        return "circle"
    }
}

struct Point {
    int x
    int y
}

// Point does NOT implement Describable

fn print_desc(T: Describable)(T x) {
    print(x.describe())
}

fn main() {
    Point p = Point { x: 1, y: 2 }
    print_desc(Point)(p)   // should fail: Point does not satisfy Describable
}
