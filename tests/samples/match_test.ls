module main

fn classify(int n) -> int {
    match n {
        0 => 0,
        1 => 1,
        _ => n + 1,
    }
}

fn main() -> int {
    int a = classify(0)
    int b = classify(1)
    int c = classify(5)
    return 0
}
