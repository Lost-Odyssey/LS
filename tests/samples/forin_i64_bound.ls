// Regression: `for i in 0..n` / `for i in n` with an i64 bound used to emit
// `icmp i32 %cur, i64 %end` and fail LLVM module verification. The range loop
// variable is i32 (the checker types it as int); codegen now coerces i64 bounds
// to the i32 counter. Covers: i64 param bound, i64 local bound, i64 start, and
// a single i64 expression bound.
def sumto(i64 n) -> i64 {
    i64 acc = 0
    for i in 0..n { acc = acc + (i as i64) }
    return acc
}

def main() {
    i64 a = sumto(100)        // sum 0..99 = 4950

    i64 m = 50                // i64 local as upper bound
    i64 c = 0
    for i in 0..m { c = c + 1 }

    i64 d = 0                 // single i64 expression bound (0..n form)
    for i in m { d = d + 1 }

    i64 lo = 10               // i64 start bound, literal end
    i64 e = 0
    for i in lo..20 { e = e + 1 }

    bool ok = true
    if a != 4950 { ok = false }
    if c != 50 { ok = false }
    if d != 50 { ok = false }
    if e != 10 { ok = false }
    if ok {
        @print("FORINI64 PASS")
    } else {
        @print(f"FORINI64 FAIL a={a} c={c} d={d} e={e}")
    }
}
