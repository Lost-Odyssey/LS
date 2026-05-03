/* Phase 5.8: &struct (read-only) — auto-borrow + field reads. */
struct Point {
    f64 x;
    f64 y;
}

fn dist_sq(&Point p) -> f64 {
    return p.x * p.x + p.y * p.y
}

fn main() -> int {
    Point p = Point { x: 3.0, y: 4.0 }
    print(dist_sq(p))         /* expect: 25 */
    print(p.x)                /* expect: 3 */
    print(p.y)                /* expect: 4 */
    return 0
}
