// enum_align_test.ls — bug #25: enum payload must be aligned so f64/i64 fields
// round-trip correctly through construction + match destructuring at scale.
enum Mixed {
    Empty
    One(f64 a)
    Two(i64 x, f64 y)
    Three(i64 p, i64 q, f64 r)
}

fn sum_variant(Mixed m) -> f64 {
    match m {
        Empty => { return 0.0 }
        One(a) => { return a }
        Two(x, y) => { return x as f64 + y }
        Three(p, q, r) => { return p as f64 + q as f64 + r }
    }
}

fn main() -> int {
    // exercise all variants in a loop; values must survive aligned payload copies
    f64 acc = 0.0
    int n = 100000
    for i in 0..n {
        int k = i % 4
        Mixed m = Empty
        if k == 1 { m = One(1.5) }
        if k == 2 { m = Two(10 as i64, 2.5) }
        if k == 3 { m = Three(100 as i64, 200 as i64, 0.25) }
        acc = acc + sum_variant(m)
    }
    // per 4 iters: 0 + 1.5 + 12.5 + 300.25 = 314.25; 25000 groups → 7856250.0
    if acc > 7856249.0 && acc < 7856251.0 {
        print("ENUM_ALIGN PASS")
    } else {
        print(f"ENUM_ALIGN FAIL acc={acc:.2f}")
    }
    return 0
}
