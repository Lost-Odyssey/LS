// trait_struct_bound_reject.ls — negative test: type doesn't satisfy struct bound

interface Describable {
    def describe(&self) -> Str
}

struct Wrapper(T: Describable) {
    T item
}

struct Point {
    int x
    int y
}

// Point does NOT implement Describable — should be rejected
def main() {
    Point p = Point { x: 1, y: 2 }
    Wrapper(Point) w = Wrapper(Point) { item: p }
}
