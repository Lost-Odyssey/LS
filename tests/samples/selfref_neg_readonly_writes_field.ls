/* Neg: &self method cannot assign to self.field. */
struct Point { f64 x; f64 y; }

impl Point {
    fn bad(&self, f64 dx) {
        self.x = self.x + dx
    }
}

fn main() -> int {
    Point p = Point { x: 1.0, y: 2.0 }
    p.bad(1.0)
    return 0
}
