def main() -> int {
    Str name = "Alice"
    int age = 30
    f64 pi = 3.14159

    @print(f"Hello, {name}!")
    @print(f"Age: {age}, Pi: {pi}")
    @print(f"1 + 2 = {1 + 2}")
    @print(f"Is true? {true}")
    @print(f"No interpolation here")

    return 0
}
