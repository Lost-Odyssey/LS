// trait_extra_method.ls — negative test: extra method not in trait

trait Printable {
    fn to_string(&self) -> Str
}

struct Point {
    int x
    int y
}

// Implements to_string (correct) + extra method 'foo' (not in trait)
impl Printable for Point {
    fn to_string(&self) -> Str {
        return "point"
    }
    fn foo(&self) -> int {
        return 42
    }
}

fn main() {
    print(42)
}
