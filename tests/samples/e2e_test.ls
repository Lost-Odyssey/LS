struct Point {
    f64 x;
    f64 y;
}

methods Point {
    def sum_coords() -> f64 {
        return self.x + self.y
    }
}

def fibonacci(int n) -> int {
    if (n < 2) {
        return n
    }
    return fibonacci(n - 1) + fibonacci(n - 2)
}

def max(int a, int b) -> int {
    if (a > b) {
        return a
    } else {
        return b
    }
}

def classify(int n) -> int {
    match n {
        0 => 100,
        1 => 200,
        _ => 300,
    }
}

def main() -> int {
    // Fibonacci
    int fib10 = fibonacci(10)

    // Max
    int m = max(42, 17)

    // Match
    int c0 = classify(0)
    int c1 = classify(1)
    int c2 = classify(99)

    // Struct
    Point p
    p.x = 3.0
    p.y = 4.0

    // While loop
    int sum = 0
    int i = 0
    while (i < 10) {
        sum = sum + i
        i = i + 1
    }

    // Print
    @print("All tests completed!")

    // Return fib(10) = 55 as exit code
    return fib10
}
