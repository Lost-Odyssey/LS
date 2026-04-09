module main

struct Point {
    f64 x;
    f64 y;
}

impl Point {
    fn distance(Point other) -> f64 {
        f64 dx = self.x - other.x
        f64 dy = self.y - other.y
        return dx * dx + dy * dy
    }

    static fn origin() -> Point {
        Point p
        p.x = 0.0
        p.y = 0.0
        return p
    }
}

fn main() -> int {
    Point p
    p.x = 1.0
    p.y = 2.0
    Point p2
    p2.x = 1.0
    p2.y = 5.0
    f64 d = p.distance(p2)
    print(d)
    return 0
}
