// Polymorphic math.abs/min/max — int args dispatch to integer LLVM
// intrinsics; float args use the f64 path. Verifies precision is
// preserved (no silent f64 round-trip for integers).
import std.core.math as math

def main() -> int {
    // ---- Integer abs ----
    int x = -5
    @print(math.abs(x))           // 5  (int, no decimal point)
    @print(math.abs(-12345))      // 12345

    i64 big = -9999
    @print(math.abs(big))         // 9999

    // ---- Float abs (still works) ----
    @print(math.abs(-3.5))        // 3.500000

    // ---- Integer min/max ----
    @print(math.min(10, 20))      // 10
    @print(math.max(10, 20))      // 20
    @print(math.min(-7, -3))      // -7
    @print(math.max(-7, -3))      // -3

    // Mixed int + i64 → result is i64
    int a = 100
    i64 b = 50
    @print(math.min(a, b))        // 50
    @print(math.max(a, b))        // 100

    // ---- Float min/max ----
    @print(math.min(3.5, 2.1))    // 2.100000
    @print(math.max(3.5, 2.1))    // 3.500000

    // ---- Mixed int + float → goes to float path ----
    @print(math.max(5, 2.5))      // 5.000000

    // ---- Use int abs result in int context (no cast needed) ----
    int diff = math.abs(-42)
    @print(diff)                  // 42
    int total = diff + 8
    @print(total)                 // 50

    return 0
}
