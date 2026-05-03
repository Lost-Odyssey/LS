/* Neg: aliasing rule — cannot pass &!p and p (auto-borrow) in same call. */
struct Point { f64 x; f64 y; }

fn both(&!Point a, &Point b) { a.x = 0.0 }

fn main() -> int {
    Point p = Point { x: 1.0, y: 2.0 }
    both(&!p, p)
    return 0
}
