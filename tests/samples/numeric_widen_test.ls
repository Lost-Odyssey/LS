// Phase X: Zig-style implicit numeric widening
// Verifies:
//   - Variable init: f64 a = 5 / i64 x = int_var
//   - Function args: math.sqrt(int_var) auto-promotes
//   - Mixed arithmetic: 2.0 + 3, int + i64
//   - Return: fn returning f64 can `return 5`
//   - Comparisons across mixed numeric types
import math

fn double_it(f64 x) -> f64 { return x * 2.0 }
fn sum_long(i64 a, i64 b) -> i64 { return a + b }
fn sqrt_of(f64 x) -> f64 { return math.sqrt(x) }

fn main() -> int {
    // ---- Variable init widening ----
    f64 a = 5             // int -> f64
    i64 big = 100         // int(i32) -> i64
    print(a)              // 5.0
    print(big)            // 100

    // ---- Function call arg widening ----
    int n = 16
    print(sqrt_of(n))            // int -> f64, sqrt = 4.0
    print(double_it(7))          // int literal -> f64, = 14.0
    print(sum_long(10, 20))      // int -> i64, = 30

    // ---- Mixed arithmetic ----
    f64 c = 2.0 + 3              // f64 + int -> f64
    i64 d = 1000 + big           // int + i64 -> i64
    print(c)                     // 5.0
    print(d)                     // 1100

    // ---- Comparisons across types ----
    if n < 100 { print(1) } else { print(0) }   // int < int = true → 1
    if 3.14 > n { print(1) } else { print(0) }  // f64 > int(promoted) = false → 0

    // ---- Return widening ----
    print(double_it(big as f64)) // explicit cast still works
    return 0
}
