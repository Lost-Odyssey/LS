// Test: f-Str in expression chain (method call on f-Str result)
def main() {
    int x = 42
    Str result = f"number: {x}".upper()
    @print(result)
    @print("done")
}