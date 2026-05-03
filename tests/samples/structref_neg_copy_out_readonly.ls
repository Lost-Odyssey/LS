/* Neg: cannot copy out of &struct. */
struct Point { f64 x; f64 y; }

fn bad(&Point p) {
    Point t = p
}

fn main() -> int {
    Point p = Point { x: 1.0, y: 2.0 }
    bad(p)
    return 0
}
