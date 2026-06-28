def test_shadowed_string() -> Str {
    Str x = "outer"
    @print("outer created")
    
    {
        Str x = "inner"  // shadows outer
        @print("inner created")
        return x  // should NOT drop inner
    }
    
    // outer x is still alive here
    return x
}

def main() {
    @print("=== Test: shadowed string ===")
    Str result = test_shadowed_string()
    @print("result:")
    @print(result)
}
