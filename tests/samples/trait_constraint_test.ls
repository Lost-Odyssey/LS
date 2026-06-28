// trait_constraint_test.ls — Step 8: constrained generic function calls

interface HasValue {
    def value(&self) -> int
}

interface Describable {
    def describe(&self) -> Str
}

struct Circle {
    int radius
}

methods Circle: HasValue {
    def value(&self) -> int {
        return self.radius
    }
}

methods Circle: Describable {
    def describe(&self) -> Str {
        return "circle"
    }
}

struct Square {
    int side
}

methods Square: Describable {
    def describe(&self) -> Str {
        return "square"
    }
}

// Single bound
def get_value(T: HasValue)(T x) -> int {
    return x.value()
}

// Single bound, returns Str
def get_desc(T: Describable)(T x) -> Str {
    return x.describe()
}

// Multiple bounds
def desc_and_val(T: Describable + HasValue)(T x) -> int {
    @print(x.describe())
    return x.value()
}

def main() {
    Circle c = Circle { radius: 5 }
    Square s = Square { side: 10 }

    @print(get_value(Circle)(c))     // 5
    @print(get_desc(Circle)(c))      // circle
    @print(get_desc(Square)(s))      // square
    @print(desc_and_val(Circle)(c))  // circle, then 5
    @print(99)
}
