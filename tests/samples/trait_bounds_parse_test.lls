// trait_bounds_parse_test.ls — Step 7: verify trait bounds syntax parses

interface Printable {
    def to_string(&self) -> Str
}

interface HasValue {
    def value(&self) -> int
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

methods Point: HasValue {
    def value(&self) -> int {
        return self.x + self.y
    }
}

// Constrained generic function: T must implement Printable
def print_it(T: Printable)(T item) {
    @print(item.to_string())
}

// Multiple bounds: T: Printable + HasValue
def describe(T: Printable + HasValue)(T item) {
    @print(item.to_string())
    @print(item.value())
}

// Mixed: one param bounded, one unbounded
def mixed(T: Printable, U)(T item, U other) -> int {
    return 42
}

def main() {
    // For now, just verify parsing works. Actual constrained calls are Step 8.
    Point p = Point { x: 10, y: 20 }
    @print(p.to_string())
    @print(p.value())
    @print(99)
}
