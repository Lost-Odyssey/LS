/* Neg: static def cannot have &!self / &self. */
struct Point { f64 x; f64 y; }

methods Point {
    static def make(&!self) {
        @print(1)
    }
}

def main() -> int {
    return 0
}
