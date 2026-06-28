/* Neg: &!struct requires explicit &! at call site. */
struct Point { f64 x; f64 y; }

def bad(&!Point p) {
    p.x = 0.0
}

def main() -> int {
    Point p = Point { x: 1.0, y: 2.0 }
    bad(p)
    return 0
}
