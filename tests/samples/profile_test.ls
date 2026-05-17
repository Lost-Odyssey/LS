/* profile_test.ls — Verify --profile produces valid output for common patterns.
   Tested via ls run --profile; output checked externally for correctness. */

fn factorial(int n) -> int {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}

fn sum_range(int lo, int hi) -> int {
    int s = 0
    int i = lo
    while i <= hi {
        s = s + i
        i = i + 1
    }
    return s
}

fn helper(int x) -> int {
    return x * x + factorial(x)
}

fn main() {
    int f = factorial(10)
    print(f)          /* 3628800 */

    int s = sum_range(1, 100)
    print(s)          /* 5050 */

    int h = helper(5)
    print(h)          /* 25 + 120 = 145 */

    print("ALL PASS")
}
