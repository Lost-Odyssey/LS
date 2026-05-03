/* Neg: instance methods on struct borrow are not yet supported. */
struct Point { f64 x; f64 y; }

impl Point {
    fn show() {
        print(self.x)
    }
}

fn bad(&Point p) {
    p.show()
}

fn main() -> int {
    Point p = Point { x: 1.0, y: 2.0 }
    bad(p)
    return 0
}
