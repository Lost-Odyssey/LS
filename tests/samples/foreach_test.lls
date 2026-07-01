// foreach loop tests — for var in iterable { body }

def main() {
    // Range iteration: for i in 0..5 { }
    int sum = 0
    for i in 0..5 {
        sum = sum + i
    }
    @print(sum)          // 0+1+2+3+4 = 10

    // Range with expressions: for i in lo..hi { }
    int sum2 = 0
    int lo = 3
    int hi = 8
    for i in lo..hi {
        sum2 = sum2 + i
    }
    @print(sum2)         // 3+4+5+6+7 = 25

    // Iterate over a count: for i in n { } means for i in 0..n
    int sum3 = 0
    for i in 5 {
        sum3 = sum3 + i
    }
    @print(sum3)         // 0+1+2+3+4 = 10

    // Foreach with break
    int count = 0
    for i in 0..100 {
        if (i == 7) {
            break
        }
        count = count + 1
    }
    @print(count)        // 7

    // Foreach with continue — sum even numbers only
    int even_sum = 0
    for i in 0..10 {
        if (i % 2 != 0) {
            continue
        }
        even_sum = even_sum + i
    }
    @print(even_sum)     // 0+2+4+6+8 = 20

    // Nested foreach
    int pairs = 0
    for i in 0..3 {
        for j in 0..4 {
            pairs = pairs + 1
        }
    }
    @print(pairs)        // 3 * 4 = 12

    // With parentheses: for (i in 0..5) { }
    int sum4 = 0
    for (i in 0..5) {
        sum4 = sum4 + i
    }
    @print(sum4)         // 10
}
