/* Neg: cannot call &!self method through &Struct readonly borrow. */
struct Point { f64 x; f64 y; }

impl Point {
    fn shift(&!self, f64 dx) {
        self.x = self.x + dx
    }
}

fn bad(&Point p) {
    p.shift(1.0)
}

fn main() -> int {
    Point p = Point { x: 1.0, y: 2.0 }
    bad(p)
    return 0
}
