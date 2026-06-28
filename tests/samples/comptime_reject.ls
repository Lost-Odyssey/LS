// Negative: comptime for requires a struct in fields(...). A non-struct type
// (here a primitive) must be a clean compile error, not a crash.

def main() -> int {
    comptime for f in fields(int) {
        @print(f.name)
    }
    @print("unreachable")
    return 0
}
