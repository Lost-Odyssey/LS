// vec_test.ls — vec(T) comprehensive end-to-end test

// ---- Global vec ----
vec(int) g_scores

fn fill_scores() {
    g_scores.push(90)
    g_scores.push(85)
    g_scores.push(92)
    g_scores.push(88)
}

fn sum_scores() -> int {
    int total = 0
    for s in g_scores {
        total = total + s
    }
    return total
}

// ---- Helper: sum a vec(int) ----
fn vec_sum(vec(int) v) -> int {
    int s = 0
    for x in v { s = s + x }
    return s
}

fn main() -> int {
    // === 1. Basic push / index / length ===
    vec(int) v
    v.push(10)
    v.push(20)
    v.push(30)
    print(v.length)     // 3
    print(v[0])         // 10
    print(v[1])         // 20
    print(v[2])         // 30

    // === 2. Index write ===
    v[1] = 99
    print(v[1])         // 99

    // === 3. for-in iteration + sum ===
    int s = 0
    for x in v { s = s + x }
    print(s)            // 139  (10+99+30)

    // === 4. pop ===
    v.pop()
    print(v.length)     // 2
    print(v[0])         // 10

    // === 5. clear and reuse ===
    v.clear()
    print(v.length)     // 0
    v.push(7)
    v.push(8)
    v.push(9)
    print(v.length)     // 3
    print(vec_sum(v))   // 24

    // === 6. reserve ===
    vec(int) big
    big.reserve(50)
    print(big.length)       // 0
    print(big.capacity >= 50)  // 1  (true)
    big.push(42)
    print(big[0])           // 42

    // === 7. grow beyond initial cap (triggers realloc) ===
    vec(int) grow_v
    for i in 0..20 { grow_v.push(i) }
    print(grow_v.length)    // 20
    int gsum = 0
    for x in grow_v { gsum = gsum + x }
    print(gsum)             // 190  (0+1+...+19)

    // === 8. vec(string) ===
    vec(string) words
    words.push("hello")
    words.push("world")
    words.push("foo")
    print(words.length)     // 3
    int clen = 0
    for w in words { clen = clen + w.length }
    print(clen)             // 13  (5+5+3)

    // === 9. vec(string) index write frees old ===
    words[0] = "hello".upper()    // frees static "hello", stores "HELLO"
    print(words[0])               // HELLO

    // === 10. vec(f64) ===
    vec(f64) floats
    floats.push(1.5)
    floats.push(2.5)
    floats.push(3.0)
    f64 fsum = 0.0
    for x in floats { fsum = fsum + x }
    print(fsum)             // 7.000000

    // === 11. multiple vecs in same scope ===
    vec(int) a
    vec(int) b
    for i in 0..3 { a.push(i + 1) }      // 1 2 3
    for i in 0..3 { b.push((i + 1) * 10) } // 10 20 30
    print(vec_sum(a))   // 6
    print(vec_sum(b))   // 60

    // === 12. global vec ===
    fill_scores()
    print(sum_scores())      // 355  (90+85+92+88)
    print(g_scores.length)   // 4

    // === 13. empty vec — no alloc ===
    vec(int) empty
    print(empty.length)      // 0
    print(empty.capacity)    // 0

    return 0
}
