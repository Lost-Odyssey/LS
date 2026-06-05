// struct_loop_test.ls — bug #24: struct literal alloca in loop body caused
// stack overflow in JIT (entry-block fix). Tests struct construction + method
// call in a large loop. n=200000 would crash before the fix (JIT 1MB stack,
// 200k × 16B alloca = 3.2MB).
struct Point { f64 x; f64 y }
impl Point {
    fn dist(&self) -> f64 { return self.x * self.x + self.y * self.y }
}

fn main() -> int {
    int n = 200000
    f64 sum = 0.0
    for i in 0..n {
        Point p = Point { x: i as f64, y: (i * 2) as f64 }
        sum = sum + p.dist()
    }
    // Expected: sum of (i^2 + (2i)^2) = sum of 5*i^2 for i=0..199999
    // = 5 * 199999 * 200000 * 399999 / 6 = 13333266667000000
    if sum > 1.3e16 && sum < 1.4e16 {
        print("STRUCT_LOOP PASS")
    } else {
        print(f"STRUCT_LOOP FAIL sum={sum:.1f}")
    }
    return 0
}
