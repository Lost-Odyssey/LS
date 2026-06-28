// Phase 8: enum with payload variants — primitive payloads + match destructure

enum Shape {
    Point
    Circle(int r)
    Rect(int w, int h)
}

def area(Shape s) -> int {
    match s {
        Point => 0
        Circle(r) => r * r * 3        // π ≈ 3
        Rect(w, h) => w * h
    }
}

def main() -> int {
    Shape s1 = Circle(5)
    Shape s2 = Rect(3, 4)
    Shape s3 = Point

    @print(area(s1))    // 75
    @print(area(s2))    // 12
    @print(area(s3))    // 0
    return 0
}
