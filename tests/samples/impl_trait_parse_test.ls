// impl_trait_parse_test.ls — Step 4: verify impl Trait for Struct parsing

trait Printable {
    fn to_string(&self) -> string
}

trait Comparable {
    fn compare(&self, int other) -> int
}

struct Point {
    int x
    int y
}

impl Printable for Point {
    fn to_string(&self) -> string {
        return "point"
    }
}

impl Comparable for Point {
    fn compare(&self, int other) -> int {
        return self.x - other
    }
}

// Regular impl still works
impl Point {
    static fn origin() -> Point {
        return Point { x: 0, y: 0 }
    }
}

fn main() {
    Point p = Point { x: 10, y: 20 }
    print(p.x)
    print(p.y)
    print(99)
}
