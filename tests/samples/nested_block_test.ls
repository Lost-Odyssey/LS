fn test_nested_no_shadow() -> string {
    string x = "hello"
    {
        print("inner block")
    }
    return x
}

fn main() {
    string result = test_nested_no_shadow()
    print(result)
}
