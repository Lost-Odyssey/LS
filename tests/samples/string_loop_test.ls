// string_loop_test.ls — bug #26: string methods that build temp stack slots
// (upper/lower/substr/replace/split) crashed JIT when called in a large loop
// (per-iteration alloca grew the stack). Fixed by entry-block allocas.
// n=200000 would overflow the 1MB JIT stack before the fix.
// Phase 2.5: split now returns std.vec Vec(string) from std.string.
import std.vec
import std.str
fn main() -> int {
    int n = 200000
    Str base = "The Quick Brown Fox"

    i64 acc = 0
    for i in 0..n {
        Str u = base.upper()
        Str l = base.lower()
        Str s = base.substr(4, 5)
        Str r = base.replace("o", "0")
        Vec(Str) parts = base.split(" ")
        acc = acc + u.len() as i64 + l.len() as i64 + s.len() as i64
              + r.len() as i64 + parts.len() as i64
    }

    // per iter: upper 19 + lower 19 + substr 5 + replace 19 + parts 4 = 66
    if acc == 66 * n {
        print("STRING_LOOP PASS")
    } else {
        print(f"STRING_LOOP FAIL acc={acc} expected={66 * n}")
    }
    return 0
}
