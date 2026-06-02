// fstring_spec_test.ls — f-string format specifier (Phase 0.3).
// Prints "FSPEC PASS" on success, "FSPEC FAIL: ..." on any mismatch.

fn check(string got, string want, string name) -> bool {
    if got == want { return true }
    print("FSPEC FAIL: " + name + " got=[" + got + "] want=[" + want + "]")
    return false
}

fn main() {
    bool ok = true

    // ---- float precision ----
    ok = check(f"{3.14159:.2f}", "3.14", "f.2") && ok
    ok = check(f"{3.14159:.0f}", "3", "f.0") && ok
    ok = check(f"{-2.5:.1f}", "-2.5", "f.neg") && ok
    f64 px = 12.345
    ok = check(f"x={px:.1f}", "x=12.3", "f.embed") && ok

    // ---- int width / zero-pad ----
    ok = check(f"{7:03d}", "007", "d.zero") && ok
    ok = check(f"{42:5d}", "   42", "d.width") && ok
    ok = check(f"{255:x}", "ff", "d.hex") && ok

    // ---- int with float spec (auto widen) ----
    ok = check(f"{42:.2f}", "42.00", "int.tofloat") && ok

    // ---- empty spec falls back to default ----
    ok = check(f"{5:}", "5", "empty.int") && ok

    // ---- multiple specs + plain interps in one string ----
    int n = 3
    ok = check(f"[{n}] {1.5:.2f}/{2.0:.0f}", "[3] 1.50/2", "mixed") && ok

    // ---- struct-literal colon at depth>=2 not misread as spec ----
    // (no struct here; verify nested braces in text still fine)
    ok = check(f"{1.0:.1f} done", "1.0 done", "trailing") && ok

    // ---- literal '%' in a no-interpolation f-string must NOT be doubled ----
    ok = check(f"100%", "100%", "pct.noexpr") && ok
    ok = check(f"width=\"50%\"", "width=\"50%\"", "pct.noexpr2") && ok
    // '%' alongside interpolation (sprintf path) stays correct too
    int done = 50
    ok = check(f"{done}% done", "50% done", "pct.withexpr") && ok

    // ---- print() fast path must match string-building path ----
    print(f"PRINTPATH {9.87654:.3f}")   // expect: PRINTPATH 9.877

    if ok { print("FSPEC PASS") }
}
