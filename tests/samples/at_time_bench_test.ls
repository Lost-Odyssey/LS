/* at_time_bench_test.ls — Tests for @time and @bench annotations */

fn fib(int n) -> int {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}

fn slow_sum(int n) -> int {
    int s = 0
    int i = 0
    while i < n {
        s = s + i
        i = i + 1
    }
    return s
}

fn main() {
    /* T1: @time on a simple expression — result value is returned */
    int v = @time fib(10)
    print(v)       /* 55 */

    /* T2: @time on an integer literal — near-zero elapsed */
    int lit = @time 42
    print(lit)     /* 42 */

    /* T3: @time on a function call that returns Str */
    Str s = @time "hello".upper()
    print(s)       /* HELLO */

    /* T4: @bench returns f64 (mean ns); we only check it's >= 0 */
    f64 mean = @bench(10) fib(15)
    if mean >= 0.0 {
        print("bench ok")
    }

    /* T5: @bench with larger n */
    f64 mean2 = @bench(100) slow_sum(1000)
    if mean2 >= 0.0 {
        print("bench2 ok")
    }

    /* T6: @time result used directly in expression */
    int doubled = (@time fib(8)) * 2
    print(doubled)   /* fib(8)=21, doubled=42 */

    /* T7: @bench with count = 1 */
    f64 mean_one = @bench(1) slow_sum(100)
    if mean_one >= 0.0 {
        print("bench1 ok")
    }

    print("ALL PASS")
}
