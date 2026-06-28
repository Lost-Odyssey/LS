// i64_literal_test.ls — bug #23: integer literals that don't fit in i32 were
// truncated to i32 at codegen (always emitted i32 const). Now the checker types
// out-of-i32-range literals as i64 and codegen emits the matching width.
import std.core.str

def main() -> int {
    int pass = 0
    int fail = 0

    i64 a = 9000000000          // > i32 max
    if a == 9000000000 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL a") }

    i64 b = 0 - 5000000000      // large negative
    if b == 0 - 5000000000 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL b") }

    i64 c = 9000000000 + 1      // literal in arithmetic
    if c == 9000000001 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL c") }

    i64 h = 0xFFFFFFFFFF         // hex literal > i32
    if h == 1099511627775 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL h") }

    // f-string interpolation of a large i64
    Str s = f"{a}"
    if s.eq?("9000000000") { pass = pass + 1 } else { fail = fail + 1; @print("FAIL fstr") }

    // small int literals must stay i32 / unaffected
    int x = 42
    if x == 42 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL x") }

    // i64 variable arithmetic still correct (not regressed)
    i64 m = 100
    i64 d = m * 50000000
    if d == 5000000000 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL d") }

    @print(f"pass={pass} fail={fail}")
    if fail == 0 { @print("I64_LITERAL PASS") } else { @print("I64_LITERAL FAIL") }
    return 0
}
