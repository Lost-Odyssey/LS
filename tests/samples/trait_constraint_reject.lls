// trait_constraint_reject.ls — negative: type doesn't satisfy trait bound

interface Describable {
    def describe(&self) -> Str
}

struct Circle {
    int radius
}

methods Circle: Describable {
    def describe(&self) -> Str {
        return "circle"
    }
}

struct Point {
    int x
    int y
}

// Point does NOT implement Describable

def print_desc(T: Describable)(T x) {
    @print(x.describe())
}

def main() {
    Point p = Point { x: 1, y: 2 }
    print_desc(Point)(p)   // should fail: Point does not satisfy Describable
}
