fn factorial(int n) -> int {
    match n {
        0 => 1,
        _ => n * factorial(n - 1),
    }
}

fn main() -> int {
    int result = factorial(5)
    return result
}
