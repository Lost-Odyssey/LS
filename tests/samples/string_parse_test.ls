fn main() {
    int pass = 0
    int fail = 0

    // === to_int: basic decimal ===
    Result(int, string) r1 = "42".to_int()
    match r1 {
        Ok(v) => {
            if v == 42 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_int 42") }
        }
        Err(e) => { fail = fail + 1; print("FAIL: to_int 42 got Err") }
    }

    // === to_int: negative ===
    Result(int, string) r2 = "-100".to_int()
    match r2 {
        Ok(v) => {
            if v == -100 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_int -100") }
        }
        Err(e) => { fail = fail + 1; print("FAIL: to_int -100 got Err") }
    }

    // === to_int: zero ===
    Result(int, string) r3 = "0".to_int()
    match r3 {
        Ok(v) => {
            if v == 0 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_int 0") }
        }
        Err(e) => { fail = fail + 1; print("FAIL: to_int 0 got Err") }
    }

    // === to_int: hex prefix ===
    Result(int, string) r4 = "0xFF".to_int()
    match r4 {
        Ok(v) => {
            if v == 255 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_int 0xFF") }
        }
        Err(e) => { fail = fail + 1; print("FAIL: to_int 0xFF got Err") }
    }

    // === to_int: invalid string ===
    Result(int, string) r5 = "abc".to_int()
    match r5 {
        Ok(v) => { fail = fail + 1; print("FAIL: to_int abc should be Err") }
        Err(e) => { pass = pass + 1 }
    }

    // === to_int: empty string ===
    Result(int, string) r6 = "".to_int()
    match r6 {
        Ok(v) => { fail = fail + 1; print("FAIL: to_int empty should be Err") }
        Err(e) => { pass = pass + 1 }
    }

    // === to_int: trailing garbage ===
    Result(int, string) r7 = "42abc".to_int()
    match r7 {
        Ok(v) => { fail = fail + 1; print("FAIL: to_int 42abc should be Err") }
        Err(e) => { pass = pass + 1 }
    }

    // === to_i64: small value (fits in i32 range) ===
    Result(i64, string) r8 = "123456".to_i64()
    match r8 {
        Ok(v) => {
            i64 expected8 = 123456
            if v == expected8 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_i64 small") }
        }
        Err(e) => { fail = fail + 1; print("FAIL: to_i64 small got Err") }
    }

    // === to_i64: negative ===
    Result(i64, string) r9 = "-987654".to_i64()
    match r9 {
        Ok(v) => {
            i64 expected9 = -987654
            if v == expected9 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_i64 neg") }
        }
        Err(e) => { fail = fail + 1; print("FAIL: to_i64 neg got Err") }
    }

    // === to_i64: invalid ===
    Result(i64, string) r10 = "not_a_number".to_i64()
    match r10 {
        Ok(v) => { fail = fail + 1; print("FAIL: to_i64 invalid should be Err") }
        Err(e) => { pass = pass + 1 }
    }

    // === to_float: basic ===
    Result(f64, string) rf1 = "3.14".to_float()
    match rf1 {
        Ok(v) => {
            if v > 3.13 {
                if v < 3.15 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_float 3.14 hi") }
            } else { fail = fail + 1; print("FAIL: to_float 3.14 lo") }
        }
        Err(e) => { fail = fail + 1; print("FAIL: to_float 3.14 got Err") }
    }

    // === to_float: scientific notation ===
    Result(f64, string) rf2 = "1e2".to_float()
    match rf2 {
        Ok(v) => {
            if v > 99.9 {
                if v < 100.1 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_float 1e2") }
            } else { fail = fail + 1; print("FAIL: to_float 1e2 lo") }
        }
        Err(e) => { fail = fail + 1; print("FAIL: to_float 1e2 got Err") }
    }

    // === to_float: integer as float ===
    Result(f64, string) rf3 = "42".to_float()
    match rf3 {
        Ok(v) => {
            if v > 41.9 {
                if v < 42.1 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_float 42") }
            } else { fail = fail + 1; print("FAIL: to_float 42 lo") }
        }
        Err(e) => { fail = fail + 1; print("FAIL: to_float 42 got Err") }
    }

    // === to_float: invalid ===
    Result(f64, string) rf4 = "xyz".to_float()
    match rf4 {
        Ok(v) => { fail = fail + 1; print("FAIL: to_float xyz should be Err") }
        Err(e) => { pass = pass + 1 }
    }

    // === to_float: empty ===
    Result(f64, string) rf5 = "".to_float()
    match rf5 {
        Ok(v) => { fail = fail + 1; print("FAIL: to_float empty should be Err") }
        Err(e) => { pass = pass + 1 }
    }

    // === to_float: negative ===
    Result(f64, string) rf6 = "-2.5".to_float()
    match rf6 {
        Ok(v) => {
            if v > -2.6 {
                if v < -2.4 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_float -2.5") }
            } else { fail = fail + 1; print("FAIL: to_float -2.5 lo") }
        }
        Err(e) => { fail = fail + 1; print("FAIL: to_float -2.5 got Err") }
    }

    // === Summary ===
    if fail == 0 {
        print("ALL PASS")
    } else {
        print(f"FAILED: {fail} tests")
    }
}
