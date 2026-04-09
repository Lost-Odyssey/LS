fn test_shadowed_string() -> string {
    string x = "outer"
    print("outer created")
    
    {
        string x = "inner"  // shadows outer
        print("inner created")
        return x  // should NOT drop inner
    }
    
    // outer x is still alive here
    return x
}

fn main() {
    print("=== Test: shadowed string ===")
    string result = test_shadowed_string()
    print("result:")
    print(result)
}
