// trait_parse_test.ls — Step 2: verify trait declaration parsing

// Simple trait with one method
trait Printable {
    fn to_string(&self) -> string
}

// Trait with multiple methods
trait Comparable {
    fn compare(&self, int other) -> int
    fn equals(&self, int other) -> bool
}

// Trait with mutable self
trait Resettable {
    fn reset(&!self)
}

// Existing code still works — trait decl is parse-only, no checker/codegen yet
struct Point {
    int x
    int y
}

impl Point {
    static fn origin() -> Point {
        return Point { x: 0, y: 0 }
    }
}

fn main() {
    Point p = Point { x: 3, y: 4 }
    print(p.x)
    print(p.y)
    print(42)
}
