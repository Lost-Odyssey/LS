import std.vec
import std.string

fn main() {
    int pass = 0
    int fail = 0

    // ── to_bool: true values ──────────────────────────────────────────────────
    Result(bool, string) b1 = "true".to_bool()
    match b1 {
        Ok(v) => { if v { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_bool 'true'") } }
        Err(e) => { fail = fail + 1; print("FAIL: to_bool 'true' got Err") }
    }

    Result(bool, string) b2 = "yes".to_bool()
    match b2 {
        Ok(v) => { if v { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_bool 'yes'") } }
        Err(e) => { fail = fail + 1; print("FAIL: to_bool 'yes' got Err") }
    }

    Result(bool, string) b3 = "1".to_bool()
    match b3 {
        Ok(v) => { if v { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_bool '1'") } }
        Err(e) => { fail = fail + 1; print("FAIL: to_bool '1' got Err") }
    }

    // ── to_bool: false values ─────────────────────────────────────────────────
    Result(bool, string) b4 = "false".to_bool()
    match b4 {
        Ok(v) => { if v { fail = fail + 1; print("FAIL: to_bool 'false' should be false") } else { pass = pass + 1 } }
        Err(e) => { fail = fail + 1; print("FAIL: to_bool 'false' got Err") }
    }

    Result(bool, string) b5 = "no".to_bool()
    match b5 {
        Ok(v) => { if v { fail = fail + 1; print("FAIL: to_bool 'no' should be false") } else { pass = pass + 1 } }
        Err(e) => { fail = fail + 1; print("FAIL: to_bool 'no' got Err") }
    }

    Result(bool, string) b6 = "0".to_bool()
    match b6 {
        Ok(v) => { if v { fail = fail + 1; print("FAIL: to_bool '0' should be false") } else { pass = pass + 1 } }
        Err(e) => { fail = fail + 1; print("FAIL: to_bool '0' got Err") }
    }

    // ── to_bool: invalid ─────────────────────────────────────────────────────
    Result(bool, string) b7 = "maybe".to_bool()
    match b7 {
        Ok(v) => { fail = fail + 1; print("FAIL: to_bool 'maybe' should be Err") }
        Err(e) => { pass = pass + 1 }
    }

    Result(bool, string) b8 = "".to_bool()
    match b8 {
        Ok(v) => { fail = fail + 1; print("FAIL: to_bool '' should be Err") }
        Err(e) => { pass = pass + 1 }
    }

    // ── lines: basic LF ───────────────────────────────────────────────────────
    Vec(string) ls1 = "hello\nworld\n".lines()
    if ls1.len() == 2 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: lines LF len={ls1.len()}") }
    if ls1[0].compare("hello") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: lines[0]={ls1[0]}") }
    if ls1[1].compare("world") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: lines[1]={ls1[1]}") }

    // ── lines: CRLF ───────────────────────────────────────────────────────────
    Vec(string) ls2 = "a\r\nb\r\nc".lines()
    if ls2.len() == 3 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: lines CRLF len={ls2.len()}") }
    if ls2[0].compare("a") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: lines CRLF[0]={ls2[0]}") }
    if ls2[1].compare("b") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: lines CRLF[1]={ls2[1]}") }
    if ls2[2].compare("c") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: lines CRLF[2]={ls2[2]}") }

    // ── lines: single line, no newline ────────────────────────────────────────
    Vec(string) ls3 = "single".lines()
    if ls3.len() == 1 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: lines single len={ls3.len()}") }
    if ls3[0].compare("single") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: lines single[0]={ls3[0]}") }

    // ── lines: empty string ───────────────────────────────────────────────────
    Vec(string) ls4 = "".lines()
    if ls4.len() == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: lines empty len={ls4.len()}") }

    // ── lines: no trailing newline ────────────────────────────────────────────
    Vec(string) ls5 = "foo\nbar".lines()
    if ls5.len() == 2 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: lines notail len={ls5.len()}") }
    if ls5[0].compare("foo") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: lines notail[0]={ls5[0]}") }
    if ls5[1].compare("bar") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: lines notail[1]={ls5[1]}") }

    // ── repeat ────────────────────────────────────────────────────────────────
    string rp1 = "ha".repeat(3)
    if rp1.compare("hahaha") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: repeat 3={rp1}") }

    string rp2 = "x".repeat(1)
    if rp2.compare("x") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: repeat 1={rp2}") }

    string rp3 = "ab".repeat(0)
    if rp3.length == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: repeat 0 len={rp3.length}") }

    string rp4 = "".repeat(10)
    if rp4.length == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: repeat empty len={rp4.length}") }

    // ── pad_left ──────────────────────────────────────────────────────────────
    // 48 = '0', 46 = '.'
    string pl1 = "42".pad_left(6, 48)
    if pl1.compare("000042") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: pad_left zero={pl1}") }

    string pl2 = "hi".pad_left(5, 46)
    if pl2.compare("...hi") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: pad_left dot={pl2}") }

    string pl3 = "long".pad_left(2, 48)
    if pl3.compare("long") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: pad_left no-trunc={pl3}") }

    string pl4 = "x".pad_left(1, 48)
    if pl4.compare("x") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: pad_left exact={pl4}") }

    // ── pad_right ─────────────────────────────────────────────────────────────
    string pr1 = "hi".pad_right(5, 46)
    if pr1.compare("hi...") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: pad_right dot={pr1}") }

    string pr2 = "42".pad_right(6, 32)
    if pr2.compare("42    ") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: pad_right space={pr2}") }

    string pr3 = "toolong".pad_right(3, 48)
    if pr3.compare("toolong") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: pad_right no-trunc={pr3}") }

    // ── chars ─────────────────────────────────────────────────────────────────
    Vec(int) cs1 = "ABC".chars()
    if cs1.len() == 3 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: chars len={cs1.len()}") }
    if cs1[0] == 65 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: chars[0]={cs1[0]}") }
    if cs1[1] == 66 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: chars[1]={cs1[1]}") }
    if cs1[2] == 67 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: chars[2]={cs1[2]}") }

    Vec(int) cs2 = "".chars()
    if cs2.len() == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: chars empty len={cs2.len()}") }

    Vec(int) cs3 = "hello".chars()
    if cs3.len() == 5 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: chars hello len={cs3.len()}") }
    if cs3[0] == 104 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: chars h={cs3[0]}") }

    // ── Summary ───────────────────────────────────────────────────────────────
    if fail == 0 {
        print("ALL PASS")
    } else {
        print(f"FAILED: {fail} tests")
    }
}
