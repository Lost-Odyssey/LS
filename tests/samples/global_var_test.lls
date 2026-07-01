// Global variable test — mutable globals across functions

int counter = 0
int MULTIPLIER = 3
f64 PI = 3.14159

def increment() {
    counter = counter + 1
}

def add_n(int n) {
    counter = counter + n
}

def get_counter() -> int {
    return counter
}

def scaled(int x) -> int {
    return x * MULTIPLIER
}

def main() -> int {
    // Initial value
    @print(counter)           // 0

    // Mutate from different functions
    increment()
    @print(counter)           // 1

    increment()
    increment()
    @print(counter)           // 3

    add_n(10)
    @print(counter)           // 13

    // Read via function
    int c = get_counter()
    @print(c)                 // 13

    // Use global constant
    @print(scaled(5))         // 15

    // Direct read of f64 global
    @print(PI)                // 3.141590

    return 0
}
