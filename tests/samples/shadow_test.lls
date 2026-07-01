def test_shadow() -> Str {
    Str x = "outer"
    {
        Str x = "inner"
        return x
    }
    return x
}

def main() {
    Str result = test_shadow()
    @print(result)
}
