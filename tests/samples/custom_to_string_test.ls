// Test: Can users define a custom to_string method?
struct Point {
    int x;
    int y;
}

impl Point {
    fn to_string() -> Str {
        return f"({self.x}, {self.y})"
    }
}

fn main() {
    Point p
    p.x = 10
    p.y = 20
    Str s = p.to_string()
    print("Point: ", s)
}
