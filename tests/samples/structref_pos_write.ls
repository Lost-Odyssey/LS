/* Phase 5.8: &!struct — field writes propagate to caller. */
struct Point {
    f64 x;
    f64 y;
}

fn translate(&!Point p, f64 dx, f64 dy) {
    p.x = p.x + dx
    p.y = p.y + dy
}

fn main() -> int {
    Point p = Point { x: 1.0, y: 2.0 }
    translate(&!p, 10.0, 20.0)
    print(p.x)                /* expect: 11 */
    print(p.y)                /* expect: 22 */
    return 0
}
