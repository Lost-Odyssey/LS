// Test: f-string in expression chain (method call on f-string result)
fn main() {
    int x = 42
    string result = f"number: {x}".upper()
    print(result)
    print("done")
}