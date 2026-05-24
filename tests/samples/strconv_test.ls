import std.strconv as sc

fn main() {
    int pass = 0
    int fail = 0

    // ── format: no placeholders ───────────────────────────────────────────────
    vec(string) empty_args = []
    string f1 = sc.format("hello world", empty_args)
    if f1.compare("hello world") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: format no-ph={f1}") }

    // ── format: one placeholder ───────────────────────────────────────────────
    vec(string) a1 = ["world"]
    string f2 = sc.format("hello {}", a1)
    if f2.compare("hello world") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: format 1-ph={f2}") }

    // ── format: three placeholders ────────────────────────────────────────────
    vec(string) a3 = [f"{1}", f"{2}", f"{3}"]
    string f3 = sc.format("{} + {} = {}", a3)
    if f3.compare("1 + 2 = 3") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: format 3-ph={f3}") }

    // ── format: more placeholders than args (extras become empty) ─────────────
    vec(string) a2 = ["A", "B"]
    string f4 = sc.format("{}-{}-{}", a2)
    if f4.compare("A-B-") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: format ovph={f4}") }

    // ── format: leading/trailing text ─────────────────────────────────────────
    vec(string) a_name = ["Alice"]
    string f5 = sc.format("Name: {}", a_name)
    if f5.compare("Name: Alice") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: format lead={f5}") }

    // ── int_to_hex: zero ─────────────────────────────────────────────────────
    string h0 = sc.int_to_hex(0)
    if h0.compare("0") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: hex 0={h0}") }

    // ── int_to_hex: 255 → "ff" ───────────────────────────────────────────────
    string h255 = sc.int_to_hex(255)
    if h255.compare("ff") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: hex 255={h255}") }

    // ── int_to_hex: 16 → "10" ────────────────────────────────────────────────
    string h16 = sc.int_to_hex(16)
    if h16.compare("10") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: hex 16={h16}") }

    // ── int_to_hex: 4096 → "1000" ────────────────────────────────────────────
    string h4096 = sc.int_to_hex(4096)
    if h4096.compare("1000") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: hex 4096={h4096}") }

    // ── int_to_hex: negative ─────────────────────────────────────────────────
    string hn16 = sc.int_to_hex(-16)
    if hn16.compare("-10") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: hex -16={hn16}") }

    // ── int_to_hex: 256 → "100" ──────────────────────────────────────────────
    string h256 = sc.int_to_hex(256)
    if h256.compare("100") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: hex 256={h256}") }

    // ── int_to_oct: zero ─────────────────────────────────────────────────────
    string o0 = sc.int_to_oct(0)
    if o0.compare("0") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: oct 0={o0}") }

    // ── int_to_oct: 8 → "10" ─────────────────────────────────────────────────
    string o8 = sc.int_to_oct(8)
    if o8.compare("10") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: oct 8={o8}") }

    // ── int_to_oct: 255 → "377" ──────────────────────────────────────────────
    string o255 = sc.int_to_oct(255)
    if o255.compare("377") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: oct 255={o255}") }

    // ── int_to_oct: negative ─────────────────────────────────────────────────
    string on8 = sc.int_to_oct(-8)
    if on8.compare("-10") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: oct -8={on8}") }

    // ── int_to_bin: zero ─────────────────────────────────────────────────────
    string b0 = sc.int_to_bin(0)
    if b0.compare("0") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: bin 0={b0}") }

    // ── int_to_bin: 1 → "1" ──────────────────────────────────────────────────
    string b1 = sc.int_to_bin(1)
    if b1.compare("1") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: bin 1={b1}") }

    // ── int_to_bin: 10 → "1010" ──────────────────────────────────────────────
    string b10 = sc.int_to_bin(10)
    if b10.compare("1010") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: bin 10={b10}") }

    // ── int_to_bin: 255 → "11111111" ─────────────────────────────────────────
    string b255 = sc.int_to_bin(255)
    if b255.compare("11111111") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: bin 255={b255}") }

    // ── int_to_bin: negative ─────────────────────────────────────────────────
    string bn4 = sc.int_to_bin(-4)
    if bn4.compare("-100") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: bin -4={bn4}") }

    // ── float_fixed: 0 decimal places ────────────────────────────────────────
    string ff0 = sc.float_fixed(3.7, 0)
    if ff0.compare("4") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: fixed 3.7/0={ff0}") }

    // ── float_fixed: 2 decimal places ────────────────────────────────────────
    string ff2 = sc.float_fixed(3.14159, 2)
    if ff2.compare("3.14") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: fixed 3.14159/2={ff2}") }

    // ── float_fixed: 4 decimal places ────────────────────────────────────────
    string ff4 = sc.float_fixed(1.0, 4)
    if ff4.compare("1.0000") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: fixed 1.0/4={ff4}") }

    // ── float_fixed: negative number ─────────────────────────────────────────
    string ffn = sc.float_fixed(-2.5, 1)
    if ffn.compare("-2.5") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: fixed -2.5/1={ffn}") }

    // ── float_fixed: zero ────────────────────────────────────────────────────
    string ffz = sc.float_fixed(0.0, 2)
    if ffz.compare("0.00") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: fixed 0.0/2={ffz}") }

    // ── to_string ────────────────────────────────────────────────────────────
    string ts1 = sc.to_string(42)
    if ts1.compare("42") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: to_string 42={ts1}") }

    string ts2 = sc.to_string(-100)
    if ts2.compare("-100") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: to_string -100={ts2}") }

    string ts3 = sc.to_string(0)
    if ts3.compare("0") == 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: to_string 0={ts3}") }

    // ── to_string_f ──────────────────────────────────────────────────────────
    string tf1 = sc.to_string_f(3.14)
    if tf1.length > 0 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_string_f 3.14 empty") }

    string tf2 = sc.to_string_f(0.0)
    if tf2.length > 0 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: to_string_f 0.0 empty") }

    // ── Summary ───────────────────────────────────────────────────────────────
    if fail == 0 {
        print("ALL PASS")
    } else {
        print(f"FAILED: {fail} tests")
    }
}
