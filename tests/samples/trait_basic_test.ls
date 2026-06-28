// trait_basic_test.ls — Step 9: end-to-end integration test

import std.core.str

interface Describable {
    def describe(&self) -> Str
}

struct Circle {
    f64 radius
}

methods Circle: Describable {
    def describe(&self) -> Str {
        return f"Circle(r={self.radius})"
    }
}

struct Square {
    f64 side
}

methods Square: Describable {
    def describe(&self) -> Str {
        return f"Square(s={self.side})"
    }
}

// Constrained generic function
def print_desc(T: Describable)(T x) {
    @print(x.describe())
}

// Constrained generic returning Str
def get_desc(T: Describable)(T x) -> Str {
    return x.describe()
}

// Multiple structs through same generic
def main() {
    Circle c = Circle { radius: 3.14 }
    Square s = Square { side: 2.0 }

    // Constrained generic calls
    print_desc(Circle)(c)
    print_desc(Square)(s)

    // Return value from constrained generic
    Str d = get_desc(Circle)(c)
    @print(d)

    // Unconstrained generic still works
    @print(99)
}
