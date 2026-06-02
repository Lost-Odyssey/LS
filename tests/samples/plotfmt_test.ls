// plotfmt_test.ls — self-verifying test for std/plotfmt.ls.
// Prints "PLOTFMT PASS" on success, "PLOTFMT FAIL: ..." on any mismatch.

import plotfmt

fn check(string got, string want, string name) -> bool {
    if got == want { return true }
    print("PLOTFMT FAIL: " + name + " got=[" + got + "] want=[" + want + "]")
    return false
}

fn main() {
    bool ok = true

    // ---- fmt_fixed ----
    ok = check(plotfmt.fmt_fixed(3.14159, 2), "3.14", "fmt_fixed.2") && ok
    ok = check(plotfmt.fmt_fixed(3.14159, 0), "3", "fmt_fixed.0") && ok
    ok = check(plotfmt.fmt_fixed(-2.5, 1), "-2.5", "fmt_fixed.neg") && ok

    // ---- fmt_auto ----
    ok = check(plotfmt.fmt_auto(0.0), "0", "fmt_auto.zero") && ok
    ok = check(plotfmt.fmt_auto(1234.5), "1234", "fmt_auto.big") && ok
    ok = check(plotfmt.fmt_auto(12.3), "12.3", "fmt_auto.10s") && ok
    ok = check(plotfmt.fmt_auto(0.0345), "0.0345", "fmt_auto.small") && ok

    // ---- fmt_sci ----
    ok = check(plotfmt.fmt_sci(0.0), "0e0", "fmt_sci.zero") && ok
    ok = check(plotfmt.fmt_sci(12345.0), "1.23e4", "fmt_sci.big") && ok
    ok = check(plotfmt.fmt_sci(0.00042), "4.20e-4", "fmt_sci.small") && ok

    // ---- fmt_time ----
    ok = check(plotfmt.fmt_time(1500000000), "1.50s", "fmt_time.s") && ok
    ok = check(plotfmt.fmt_time(2500000), "2.5ms", "fmt_time.ms") && ok
    ok = check(plotfmt.fmt_time(3000), "3us", "fmt_time.us") && ok
    ok = check(plotfmt.fmt_time(750), "750ns", "fmt_time.ns") && ok

    // ---- pad ----
    ok = check(plotfmt.pad_left("ab", 5), "   ab", "pad_left") && ok
    ok = check(plotfmt.pad_right("ab", 5), "ab   ", "pad_right") && ok
    ok = check(plotfmt.pad_left("abcdef", 3), "abcdef", "pad_left.nowrap") && ok

    // ---- clamp ----
    ok = check(f"{plotfmt.clamp_i(10, 0, 5)}", "5", "clamp_i.hi") && ok
    ok = check(f"{plotfmt.clamp_i(-1, 0, 5)}", "0", "clamp_i.lo") && ok
    ok = check(f"{plotfmt.clamp_i(3, 0, 5)}", "3", "clamp_i.mid") && ok
    ok = check(plotfmt.fmt_fixed(plotfmt.clamp_f(9.0, 0.0, 1.0), 1), "1.0", "clamp_f.hi") && ok

    // ---- color ----
    ok = check(plotfmt.rgb_to_hex(230, 25, 75), "#e6194b", "rgb_to_hex") && ok
    ok = check(plotfmt.rgb_to_hex(0, 0, 0), "#000000", "rgb_to_hex.black") && ok
    ok = check(plotfmt.rgb_to_hex(300, -5, 255), "#ff00ff", "rgb_to_hex.clamp") && ok
    ok = check(plotfmt.hsv_to_hex(0.0, 1.0, 1.0), "#ff0000", "hsv.red") && ok
    ok = check(plotfmt.hsv_to_hex(120.0, 1.0, 1.0), "#00ff00", "hsv.green") && ok
    ok = check(plotfmt.hsv_to_hex(240.0, 1.0, 1.0), "#0000ff", "hsv.blue") && ok

    if ok { print("PLOTFMT PASS") }
}
