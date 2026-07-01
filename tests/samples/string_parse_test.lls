import std.core.str

def main() {
    int pass = 0
    int fail = 0

    // === to_int: basic decimal ===
    Str s1 = "42"
    Result(int, Str) r1 = s1.to_int()
    match r1 {
        Ok(v) => {
            if v == 42 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL: to_int 42") }
        }
        Err(e) => { fail = fail + 1; @print("FAIL: to_int 42 got Err") }
    }

    // === to_int: negative ===
    Str s2 = "-100"
    Result(int, Str) r2 = s2.to_int()
    match r2 {
        Ok(v) => {
            if v == -100 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL: to_int -100") }
        }
        Err(e) => { fail = fail + 1; @print("FAIL: to_int -100 got Err") }
    }

    // === to_int: zero ===
    Str s3 = "0"
    Result(int, Str) r3 = s3.to_int()
    match r3 {
        Ok(v) => {
            if v == 0 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL: to_int 0") }
        }
        Err(e) => { fail = fail + 1; @print("FAIL: to_int 0 got Err") }
    }

    // === to_int: hex prefix ===
    Str s4 = "0xFF"
    Result(int, Str) r4 = s4.to_int()
    match r4 {
        Ok(v) => {
            if v == 255 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL: to_int 0xFF") }
        }
        Err(e) => { fail = fail + 1; @print("FAIL: to_int 0xFF got Err") }
    }

    // === to_int: invalid string ===
    Str s5 = "abc"
    Result(int, Str) r5 = s5.to_int()
    match r5 {
        Ok(v) => { fail = fail + 1; @print("FAIL: to_int abc should be Err") }
        Err(e) => { pass = pass + 1 }
    }

    // === to_int: empty string ===
    Str s6 = ""
    Result(int, Str) r6 = s6.to_int()
    match r6 {
        Ok(v) => { fail = fail + 1; @print("FAIL: to_int empty should be Err") }
        Err(e) => { pass = pass + 1 }
    }

    // === to_int: trailing garbage ===
    Str s7 = "42abc"
    Result(int, Str) r7 = s7.to_int()
    match r7 {
        Ok(v) => { fail = fail + 1; @print("FAIL: to_int 42abc should be Err") }
        Err(e) => { pass = pass + 1 }
    }

    // === to_i64: small value (fits in i32 range) ===
    Str s8 = "123456"
    Result(i64, Str) r8 = s8.to_i64()
    match r8 {
        Ok(v) => {
            i64 expected8 = 123456
            if v == expected8 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL: to_i64 small") }
        }
        Err(e) => { fail = fail + 1; @print("FAIL: to_i64 small got Err") }
    }

    // === to_i64: negative ===
    Str s9 = "-987654"
    Result(i64, Str) r9 = s9.to_i64()
    match r9 {
        Ok(v) => {
            i64 expected9 = -987654
            if v == expected9 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL: to_i64 neg") }
        }
        Err(e) => { fail = fail + 1; @print("FAIL: to_i64 neg got Err") }
    }

    // === to_i64: invalid ===
    Str s10 = "not_a_number"
    Result(i64, Str) r10 = s10.to_i64()
    match r10 {
        Ok(v) => { fail = fail + 1; @print("FAIL: to_i64 invalid should be Err") }
        Err(e) => { pass = pass + 1 }
    }

    // === to_float: basic ===
    Str f1 = "3.14"
    Result(f64, Str) rf1 = f1.to_float()
    match rf1 {
        Ok(v) => {
            if v > 3.13 {
                if v < 3.15 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL: to_float 3.14 hi") }
            } else { fail = fail + 1; @print("FAIL: to_float 3.14 lo") }
        }
        Err(e) => { fail = fail + 1; @print("FAIL: to_float 3.14 got Err") }
    }

    // === to_float: scientific notation ===
    Str f2 = "1e2"
    Result(f64, Str) rf2 = f2.to_float()
    match rf2 {
        Ok(v) => {
            if v > 99.9 {
                if v < 100.1 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL: to_float 1e2") }
            } else { fail = fail + 1; @print("FAIL: to_float 1e2 lo") }
        }
        Err(e) => { fail = fail + 1; @print("FAIL: to_float 1e2 got Err") }
    }

    // === to_float: integer as float ===
    Str f3 = "42"
    Result(f64, Str) rf3 = f3.to_float()
    match rf3 {
        Ok(v) => {
            if v > 41.9 {
                if v < 42.1 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL: to_float 42") }
            } else { fail = fail + 1; @print("FAIL: to_float 42 lo") }
        }
        Err(e) => { fail = fail + 1; @print("FAIL: to_float 42 got Err") }
    }

    // === to_float: invalid ===
    Str f4 = "xyz"
    Result(f64, Str) rf4 = f4.to_float()
    match rf4 {
        Ok(v) => { fail = fail + 1; @print("FAIL: to_float xyz should be Err") }
        Err(e) => { pass = pass + 1 }
    }

    // === to_float: empty ===
    Str f5 = ""
    Result(f64, Str) rf5 = f5.to_float()
    match rf5 {
        Ok(v) => { fail = fail + 1; @print("FAIL: to_float empty should be Err") }
        Err(e) => { pass = pass + 1 }
    }

    // === to_float: negative ===
    Str f6 = "-2.5"
    Result(f64, Str) rf6 = f6.to_float()
    match rf6 {
        Ok(v) => {
            if v > -2.6 {
                if v < -2.4 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL: to_float -2.5") }
            } else { fail = fail + 1; @print("FAIL: to_float -2.5 lo") }
        }
        Err(e) => { fail = fail + 1; @print("FAIL: to_float -2.5 got Err") }
    }

    // === Summary ===
    if fail == 0 {
        @print("ALL PASS")
    } else {
        @print(f"FAILED: {fail} tests")
    }
}
