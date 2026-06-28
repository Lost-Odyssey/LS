// trait_missing_method.ls — negative test: missing method in trait impl

interface Printable {
    def to_string(&self) -> Str
    def display(&self)
}

struct Point {
    int x
    int y
}

// Only implements to_string, missing display
methods Point: Printable {
    def to_string(&self) -> Str {
        return "point"
    }
}

def main() {
    @print(42)
}
