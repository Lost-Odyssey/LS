// impl_trait_parse_test.ls — Step 4: verify impl Trait for Struct parsing

interface Printable {if
    def to_string(&self) -> Str
}

interface Comparable {
    def compare(&self, int other) -> int
}

struct Point {
    int x
    int y
}

methods Point: Printable {
    def to_string(&self) -> Str {
        return "point"
    }
}

methods Point: Comparable {
    def compare(&self, int other) -> int {
        return self.x - other
    }
}

// Regular impl still works
methods Point {
    static def origin() -> Point {
        return Point { x: 0, y: 0 }
    }
}

def main() {
    Point p = Point { x: 10, y: 20 }
    @print(p.x)
    @print(p.y)
    @print(99)
}
