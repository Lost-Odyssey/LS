// Negative: a comptime if condition must be a compile-time constant. A runtime
// field access (p.x) is not — clean compile error, not a crash.

struct P { int x; int y }

def main() -> int {
    P p = P { x: 1, y: 2 }
    comptime for f in fields(P) {
        comptime if p.x > 0 {
            @print(f.name)
        }
    }
    @print("unreachable")
    return 0
}
