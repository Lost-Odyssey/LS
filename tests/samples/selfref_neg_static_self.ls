/* Neg: static fn cannot have &!self / &self. */
struct Point { f64 x; f64 y; }

impl Point {
    static fn make(&!self) {
        print(1)
    }
}

fn main() -> int {
    return 0
}
