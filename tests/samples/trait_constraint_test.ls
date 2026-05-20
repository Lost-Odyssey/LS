// trait_constraint_test.ls — Step 8: constrained generic function calls

trait HasValue {
    fn value(&self) -> int
}

trait Describable {
    fn describe(&self) -> string
}

struct Circle {
    int radius
}

impl HasValue for Circle {
    fn value(&self) -> int {
        return self.radius
    }
}

impl Describable for Circle {
    fn describe(&self) -> string {
        return "circle"
    }
}

struct Square {
    int side
}

impl Describable for Square {
    fn describe(&self) -> string {
        return "square"
    }
}

// Single bound
fn get_value(T: HasValue)(T x) -> int {
    return x.value()
}

// Single bound, returns string
fn get_desc(T: Describable)(T x) -> string {
    return x.describe()
}

// Multiple bounds
fn desc_and_val(T: Describable + HasValue)(T x) -> int {
    print(x.describe())
    return x.value()
}

fn main() {
    Circle c = Circle { radius: 5 }
    Square s = Square { side: 10 }

    print(get_value(Circle)(c))     // 5
    print(get_desc(Circle)(c))      // circle
    print(get_desc(Square)(s))      // square
    print(desc_and_val(Circle)(c))  // circle, then 5
    print(99)
}
