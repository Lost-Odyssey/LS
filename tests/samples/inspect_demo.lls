// Sample for `ls inspect` (static reflection Stage 1.5).
// Exercises: struct fields, instance methods (&self / &!self), static method,
// and an enum with payload variants.

struct Point {
    int x
    int y
}

methods Point {
    def area(&self) -> int {
        return self.x * self.y
    }
    def translate(&!self, int dx, int dy) {
        self.x = self.x + dx
        self.y = self.y + dy
    }
    static def origin() -> Point {
        return Point { x: 0, y: 0 }
    }
}

enum Shape {
    Circle(f64)
    Rect(f64, f64)
}

def main() {
    Point p = Point { x: 3, y: 4 }
    p.translate(1, 1)
    @print(p.area())
}
