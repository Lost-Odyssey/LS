// modfn_canonical_test.ls — Phase 1: canonical module-path calls `std.x.def()`
// resolve without an alias (docs/plan_module_fn_resolution.md). Covers:
//   * no-alias import → canonical call, owned-Str return (memcheck)
//   * aliased import → BOTH alias and canonical spellings resolve (always-bind)
//   * chained canonical calls
// Prints "MODFN PASS".
import std.sys.c as c
import std.text.strconv       // no alias — canonical path must resolve
import std.sys.time as tm    // aliased — both `tm.def()` and `std.sys.time.def()` must work

def fail(Str msg) { @print(msg); c.abort() }

def main() {
    // canonical, no-alias import, owned-Str return (exercises heap under memcheck)
    Str hx = std.text.strconv.int_to_hex(255)
    if (!hx.eq?("ff")) { fail("FAIL canonical int_to_hex") }

    // aliased module: alias spelling works
    i64 a = tm.now_unix_ms()
    if (a <= 0) { fail("FAIL alias now_unix_ms") }
    // ...and the canonical full path resolves too (Phase 1 binds both)
    i64 b = std.sys.time.now_unix_ms()
    if (b <= 0) { fail("FAIL canonical now_unix_ms") }

    // chained canonical calls + owned Str return
    Str iso = std.sys.time.iso8601(std.sys.time.now_utc())
    if (iso.len() <= 0) { fail("FAIL canonical iso8601") }

    @print("MODFN PASS")
}
