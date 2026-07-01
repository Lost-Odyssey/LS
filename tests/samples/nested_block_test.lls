def test_nested_no_shadow() -> Str {
    Str x = "hello"
    {
        @print("inner block")
    }
    return x
}

def main() {
    Str result = test_nested_no_shadow()
    @print(result)
}
