// trait_missing_method.ls — negative test: missing method in trait impl

trait Printable {
    fn to_string(&self) -> string
    fn display(&self)
}

struct Point {
    int x
    int y
}

// Only implements to_string, missing display
impl Printable for Point {
    fn to_string(&self) -> string {
        return "point"
    }
}

fn main() {
    print(42)
}
