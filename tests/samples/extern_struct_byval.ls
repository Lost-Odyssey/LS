// Phase E.2 — Windows x64 ABI lowering for extern struct (byval + sret)
//
// This file exercises three lowering paths:
//   (1) Small extern struct (≤ 8 bytes) returned in integer register — div_t
//   (2) Large extern struct (> 8 bytes) returned via sret slot — imaxdiv_t
//   (3) Compilation correctness for byval struct param (declared but not
//       round-tripped — the libc surface has no convenient by-value param)

extern {
    // div_t = { int, int } = 8 bytes. Windows x64: returned in RAX as i64.
    // Validates: small struct return → bitcast iN back to struct value.
    struct div_t {
        int quot
        int rem
    }

    // imaxdiv_t = { i64, i64 } = 16 bytes. Windows x64: returned via sret.
    // Validates: hidden first-arg pointer + post-call load from slot.
    struct imaxdiv_t {
        i64 quot
        i64 rem
    }

    fn div(int numer, int denom) -> div_t
    fn imaxdiv(i64 numer, i64 denom) -> imaxdiv_t
}

// --- (3) Byval-param compilation correctness ---
// We declare a fn that takes a large struct by value. We never call it
// (no libc surface accepts our shape), but the declaration alone must
// compile cleanly with the byval LLVM attribute attached.
extern struct BigParam {
    i64 a
    i64 b
    i64 c
    i64 d
}

extern fn _ls_phase_e2_unused_byval(BigParam p) -> int

fn main() {
    // === (1) Small struct return via integer register ===
    div_t r = div(17, 5)
    if r.quot != 3 {
        print("FAIL: div(17, 5).quot expected 3 got ")
        print(r.quot)
        return
    }
    if r.rem != 2 {
        print("FAIL: div(17, 5).rem expected 2 got ")
        print(r.rem)
        return
    }
    print("PASS: div(17, 5) returns div_t {3, 2} via register")

    div_t r2 = div(100, 7)
    if r2.quot != 14 || r2.rem != 2 {
        print("FAIL: div(100, 7) expected {14, 2}")
        return
    }
    print("PASS: div(100, 7) — multi-call clean")

    div_t r3 = div(-17, 5)
    if r3.quot != -3 || r3.rem != -2 {
        print("FAIL: div(-17, 5) expected {-3, -2}")
        print(r3.quot)
        print(r3.rem)
        return
    }
    print("PASS: div(-17, 5) — signed arithmetic clean")

    // === (2) Large struct return via sret slot ===
    // Use values that fit in int (i32) — call-site auto-widens to i64 param.
    imaxdiv_t big = imaxdiv(1000000000, 7)
    // 10^9 / 7 = 142857142, rem = 6
    i64 expect_q = 142857142
    if big.quot != expect_q {
        print("FAIL: imaxdiv quot expected 142857142 got ")
        print(big.quot)
        return
    }
    i64 expect_r = 6
    if big.rem != expect_r {
        print("FAIL: imaxdiv rem expected 6 got ")
        print(big.rem)
        return
    }
    print("PASS: imaxdiv(1e9, 7) returns imaxdiv_t via sret")

    imaxdiv_t big2 = imaxdiv(50, 6)
    i64 e8 = 8
    i64 e2 = 2
    if big2.quot != e8 || big2.rem != e2 {
        print("FAIL: imaxdiv(50, 6) expected {8, 2}")
        return
    }
    print("PASS: imaxdiv(50, 6) — sret multi-call clean")

    print("ALL PASS")
}
