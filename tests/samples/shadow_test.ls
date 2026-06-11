fn test_shadow() -> Str {
    Str x = "outer"
    {
        Str x = "inner"
        return x
    }
    return x
}

fn main() {
    Str result = test_shadow()
    print(result)
}
