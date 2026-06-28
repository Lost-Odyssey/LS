// C-style for loop tests

// Basic for loop: sum 1 to 10
def main() {
    int sum = 0
    for (int i = 0; i < 10; i = i + 1) {
        sum = sum + i
    }
    @print(sum)

    // For loop with empty init (variable declared outside)
    int j = 0
    for (; j < 5; j = j + 1) {
        sum = sum + 1
    }
    @print(sum)

    // For loop with break
    int count = 0
    for (int k = 0; k < 100; k = k + 1) {
        if (k == 5) {
            break
        }
        count = count + 1
    }
    @print(count)

    // For loop with continue
    int even_sum = 0
    for (int m = 0; m < 10; m = m + 1) {
        // skip odd numbers: m % 2 != 0
        if (m % 2 != 0) {
            continue
        }
        even_sum = even_sum + m
    }
    @print(even_sum)

    // Nested for loops
    int product = 0
    for (int a = 0; a < 3; a = a + 1) {
        for (int b = 0; b < 3; b = b + 1) {
            product = product + 1
        }
    }
    @print(product)
}
