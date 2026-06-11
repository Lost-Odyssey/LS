// trait_basic_test.ls — Step 9: end-to-end integration test

import std.str

trait Describable {
    fn describe(&self) -> Str
}

struct Circle {
    f64 radius
}

impl Describable for Circle {
    fn describe(&self) -> Str {
        return f"Circle(r={self.radius})"
    }
}

struct Square {
    f64 side
}

impl Describable for Square {
    fn describe(&self) -> Str {
        return f"Square(s={self.side})"
    }
}

// Constrained generic function
fn print_desc(T: Describable)(T x) {
    print(x.describe())
}

// Constrained generic returning Str
fn get_desc(T: Describable)(T x) -> Str {
    return x.describe()
}

// Multiple structs through same generic
fn main() {
    Circle c = Circle { radius: 3.14 }
    Square s = Square { side: 2.0 }

    // Constrained generic calls
    print_desc(Circle)(c)
    print_desc(Square)(s)

    // Return value from constrained generic
    Str d = get_desc(Circle)(c)
    print(d)

    // Unconstrained generic still works
    print(99)
}
