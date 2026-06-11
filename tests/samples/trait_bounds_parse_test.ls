// trait_bounds_parse_test.ls — Step 7: verify trait bounds syntax parses

trait Printable {
    fn to_string(&self) -> Str
}

trait HasValue {
    fn value(&self) -> int
}

struct Point {
    int x
    int y
}

impl Printable for Point {
    fn to_string(&self) -> Str {
        return "point"
    }
}

impl HasValue for Point {
    fn value(&self) -> int {
        return self.x + self.y
    }
}

// Constrained generic function: T must implement Printable
fn print_it(T: Printable)(T item) {
    print(item.to_string())
}

// Multiple bounds: T: Printable + HasValue
fn describe(T: Printable + HasValue)(T item) {
    print(item.to_string())
    print(item.value())
}

// Mixed: one param bounded, one unbounded
fn mixed(T: Printable, U)(T item, U other) -> int {
    return 42
}

fn main() {
    // For now, just verify parsing works. Actual constrained calls are Step 8.
    Point p = Point { x: 10, y: 20 }
    print(p.to_string())
    print(p.value())
    print(99)
}
