// Negative: a generic borrow-returning method whose element type is a POD scalar
// (`Box(int).get_ref(&self) -> &int`) must be rejected at compile time. Borrow
// returns are aggregate-only (struct/enum) in v1; a POD value needs no borrow.
// Expect a clean compile error (rc=1), NOT a crash or bad IR.

struct Box(T) {
    T value
}

methods(T) Box(T) {
    def get_ref(&self) -> &T { return self.value }
}

def main() -> int {
    Box(int) bi = Box(int){ value: 7 }
    @print(bi.get_ref())
    return 0
}
