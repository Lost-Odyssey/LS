// Test: Can users use to_string(obj) syntax for custom structs?
struct Point {
    int x;
    int y;
}

impl Point {
    fn to_string() -> string {
        return f"({self.x}, {self.y})"
    }
}

fn main() {
    Point p
    p.x = 10
    p.y = 20
    
    // Try to use global to_string() with Point - this should fail
    string s = to_string(p)
    print("Point: ", s)
}
