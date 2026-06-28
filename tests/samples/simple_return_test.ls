def test_simple_return() -> Str {
    Str x = "hello"
    return x
}

def main() {
    Str result = test_simple_return()
    @print(result)
}
