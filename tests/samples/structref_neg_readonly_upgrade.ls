/* Neg: &struct cannot be upgraded to &!struct. */
struct Point { f64 x; f64 y; }

fn sink(&!Point p) { p.x = 0.0 }

fn forward(&Point p) {
    sink(&!p)
}

fn main() -> int {
    Point p = Point { x: 1.0, y: 2.0 }
    forward(p)
    return 0
}
