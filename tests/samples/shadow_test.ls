fn test_shadow() -> string {
    string x = "outer"
    {
        string x = "inner"
        return x
    }
    return x
}

fn main() {
    string result = test_shadow()
    print(result)
}
