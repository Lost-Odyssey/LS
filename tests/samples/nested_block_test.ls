fn test_nested_no_shadow() -> Str {
    Str x = "hello"
    {
        print("inner block")
    }
    return x
}

fn main() {
    Str result = test_nested_no_shadow()
    print(result)
}
