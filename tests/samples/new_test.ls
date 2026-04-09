struct Point {
    f64 x;
    f64 y;
}

impl Point {
    fn length() -> f64 {
        sqrt(self.x * self.x + self.y * self.y)
    }

    fn to_string() -> string {
        return f"({self.x}, {self.y})"
    }

    static fn create(f64 x, f64 y) -> *Point {
        return new Point { x: x, y: y }
    }
}

fn main() -> int {
    // 1. Basic heap allocation + field initialization
    *Point p = new Point { x: 3.0, y: 4.0 }
    print(p.x, p.y)                  // 3.000000 4.000000

    // 2. Zero initialization
    *Point p2 = new Point
    print(p2.x, p2.y)                // 0.000000 0.000000

    // 3. Field write via pointer
    p2.x = 10.0
    p2.y = 20.0
    print(p2.x, p2.y)                // 10.000000 20.000000

    // 4. Method call on heap pointer
    f64 len = p.length()
    print(len)                        // 5.000000

    // 5. nil assignment and nil check
    *Point p3 = nil
    if (p3 == nil) {
        print("p3 is nil")            // p3 is nil
    }

    // 6. Assign and non-nil check
    p3 = new Point { x: 1.0, y: 1.0 }
    if (p3 != nil) {
        print("p3 is not nil")        // p3 is not nil
    }

    // 7. Static method returning heap pointer
    *Point p4 = Point.create(5.0, 12.0)
    print(p4.length())                // 13.000000

    // 8. Manual free
    free(p)
    free(p2)
    free(p3)
    free(p4)
    print("done")                     // done

    return 0
}
