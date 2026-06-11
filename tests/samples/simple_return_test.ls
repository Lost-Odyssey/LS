fn test_simple_return() -> Str {
    Str x = "hello"
    return x
}

fn main() {
    Str result = test_simple_return()
    print(result)
}
