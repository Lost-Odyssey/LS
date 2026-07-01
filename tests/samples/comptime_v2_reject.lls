// Negative: variants(T) requires an ENUM. Iterating a struct's "variants" is a
// clean compile error (not a crash, not silently empty).

struct Point { int x; int y }

def bad(T)() {
    comptime for vr in variants(T) {
        @print(vr.name)
    }
}

def main() -> int {
    bad(Point)()       // <-- Point is a struct, not an enum
    return 0
}
