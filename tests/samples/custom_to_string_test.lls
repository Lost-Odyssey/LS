// Test: Can users define a custom to_string method?
struct Point {
    int x;
    int y;
}

methods Point {
    def to_string() -> Str {
        return f"({self.x}, {self.y})"
    }
}

def main() {
    Point p
    p.x = 10
    p.y = 20
    Str s = p.to_string()
    @print("Point: ", s)
}
