// trait_parse_test.ls — Step 2: verify trait declaration parsing

// Simple trait with one method
interface Printable {
    def to_string(&self) -> Str
}

// Trait with multiple methods
interface Comparable {
    def compare(&self, int other) -> int
    def equals(&self, int other) -> bool
}

// Trait with mutable self
interface Resettable {
    def reset(&!self)
}

// Existing code still works — trait decl is parse-only, no checker/codegen yet
struct Point {
    int x
    int y
}

methods Point {
    static def origin() -> Point {
        return Point { x: 0, y: 0 }
    }
}

def main() {
    Point p = Point { x: 3, y: 4 }
    @print(p.x)
    @print(p.y)
    @print(42)
}
