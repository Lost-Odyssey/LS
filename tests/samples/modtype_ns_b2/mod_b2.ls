module mod_b2

struct Point {
    int x
    int y
}

enum Color {
    Red
    Green
    Blue
}

fn make_point(int x, int y) -> Point {
    Point p
    p.x = x
    p.y = y
    return p
}

fn sum(Point p) -> int {
    return p.x + p.y
}

fn color_id(Color c) -> int {
    match c {
        Red   => { return 0 }
        Green => { return 1 }
        Blue  => { return 2 }
    }
}
