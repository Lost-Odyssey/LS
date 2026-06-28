// trait_extra_method.ls — negative test: extra method not in trait

interface Printable {
    def to_string(&self) -> Str
}

struct Point {
    int x
    int y
}

// Implements to_string (correct) + extra method 'foo' (not in trait)
methods Point: Printable {
    def to_string(&self) -> Str {
        return "point"
    }
    def foo(&self) -> int {
        return 42
    }
}

def main() {
    @print(42)
}
