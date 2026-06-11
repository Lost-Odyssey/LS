// vec_test.ls — Vec(T) comprehensive end-to-end test
import std.vec

// ---- Global vec ----
Vec(int) g_scores = {}

fn fill_scores() {
    g_scores.push(90)
    g_scores.push(85)
    g_scores.push(92)
    g_scores.push(88)
}

fn sum_scores() -> int {
    int total = 0
    for (int i = 0; i < g_scores.len(); i = i + 1) {
        int s = g_scores[i]
        total = total + s
    }
    return total
}

// ---- Helper: sum a Vec(int) ----
fn vec_sum(Vec(int) v) -> int {
    int s = 0
    for (int i = 0; i < v.len(); i = i + 1) {
        int x = v[i]
        s = s + x
    }
    return s
}

fn main() -> int {
    // === 1. Basic push / index / length ===
    Vec(int) v = {}
    v.push(10)
    v.push(20)
    v.push(30)
    print(v.len())      // 3
    print(v[0])         // 10
    print(v[1])         // 20
    print(v[2])         // 30

    // === 2. Index write ===
    v[1] = 99
    print(v[1])         // 99

    // === 3. for-in iteration + sum ===
    int s = 0
    for (int i = 0; i < v.len(); i = i + 1) {
        int x = v[i]
        s = s + x
    }
    print(s)            // 139  (10+99+30)

    // === 4. pop ===
    v.pop()
    print(v.len())      // 2
    print(v[0])         // 10

    // === 5. clear and reuse ===
    v.clear()
    print(v.len())      // 0
    v.push(7)
    v.push(8)
    v.push(9)
    print(v.len())      // 3
    print(vec_sum(v))   // 24

    // === 6. reserve ===
    Vec(int) big = {}
    big.reserve(50)
    print(big.len())        // 0
    print(big.cap() >= 50)  // 1  (true)
    big.push(42)
    print(big[0])           // 42

    // === 7. grow beyond initial cap (triggers realloc) ===
    Vec(int) grow_v = {}
    for i in 0..20 { grow_v.push(i) }
    print(grow_v.len())     // 20
    int gsum = 0
    for (int i = 0; i < grow_v.len(); i = i + 1) {
        int x = grow_v[i]
        gsum = gsum + x
    }
    print(gsum)             // 190  (0+1+...+19)

    // === 8. Vec(Str) ===
    Vec(Str) words = {}
    words.push("hello")
    words.push("world")
    words.push("foo")
    print(words.len())      // 3
    int clen = 0
    for (int i = 0; i < words.len(); i = i + 1) {
        Str w = words[i]
        clen = clen + w.len()
    }
    print(clen)             // 13  (5+5+3)

    // === 9. Vec(Str) index write frees old ===
    words[0] = "hello".upper()    // frees static "hello", stores "HELLO"
    print(words[0])               // HELLO

    // === 10. Vec(f64) ===
    Vec(f64) floats = {}
    floats.push(1.5)
    floats.push(2.5)
    floats.push(3.0)
    f64 fsum = 0.0
    for (int i = 0; i < floats.len(); i = i + 1) {
        f64 x = floats[i]
        fsum = fsum + x
    }
    print(fsum)             // 7.000000

    // === 11. multiple vecs in same scope ===
    Vec(int) a = {}
    Vec(int) b = {}
    for i in 0..3 { a.push(i + 1) }      // 1 2 3
    for i in 0..3 { b.push((i + 1) * 10) } // 10 20 30
    print(vec_sum(a))   // 6
    print(vec_sum(b))   // 60

    // === 12. global vec ===
    fill_scores()
    print(sum_scores())      // 355  (90+85+92+88)
    print(g_scores.len())    // 4
    g_scores.clear()
    g_scores.shrink_to_fit()

    // === 13. empty vec — no alloc ===
    Vec(int) empty = {}
    print(empty.len())       // 0
    print(empty.cap())       // 0

    return 0
}
