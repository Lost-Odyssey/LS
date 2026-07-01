// std.core.test — native unit-test assertions + harness.
//
// Write tests as zero-arg functions named `test_*`; run them with `ls test`.
// Assertions record failures into module-global counters and RETURN (they do
// not abort), so one test can report several failures and the run continues.
//
//   import std.core.test as t
//   def test_add() {
//       t.expect(add(2, 3) == 5)        // universal: any boolean
//       t.expect_eq_int(add(2, 3), 5)   // typed: prints left/right on failure
//   }
//
// `ls test` generates a driver that calls start()/<test>/finish() per test and
// report() at the end; report() exits non-zero if anything failed.
//
// NOTE: assertions are intentionally non-generic. A generic `expect_eq(T)` can't
// yet be called across a module boundary (generic free functions aren't exported
// — they have no concrete resolved type). Use `expect(a == b)` for arbitrary
// types, or the typed helpers below for nicer failure messages.

import std.core.str
import std.sys.c as c

// ---- harness state (module globals; one shared instance per process) ----
int __t_total = 0       // tests started
int __t_failed = 0      // tests with >=1 failure
int __t_curfail = 0     // failures in the current test
Str __t_cur = ""        // current test name

def start(Str name) {
    __t_cur = name
    __t_curfail = 0
    __t_total = __t_total + 1
}

def finish() {
    if __t_curfail == 0 {
        @print(f"  ok    {__t_cur}")
    } else {
        __t_failed = __t_failed + 1
        @print(f"  FAIL  {__t_cur}")
    }
}

// Record one failure under the current test.
def fail(Str msg) {
    __t_curfail = __t_curfail + 1
    @print(f"        {msg}")
}

// Print the summary and exit non-zero on any failure (called by the driver).
def report() {
    int passed = __t_total - __t_failed
    @print(f"{passed} passed, {__t_failed} failed  ({__t_total} tests)")
    if __t_failed > 0 { c.__ls_proc_exit(1) }
}

// ---- assertions ----

// Universal: works for any condition (a == b, a < b, o.is_some?, ...).
def expect(bool cond) {
    if !cond { fail("expect(false)") }
}

def expect_eq_int(int a, int b) {
    if a != b { fail(f"expect_eq_int failed: left={a} right={b}") }
}

def expect_eq_str(Str a, Str b) {
    if !a.eq?(b) { fail(f"expect_eq_str failed: left={a} right={b}") }
}

def expect_eq_f64(f64 a, f64 b) {
    if a != b { fail(f"expect_eq_f64 failed: left={a} right={b}") }
}

def expect_near(f64 a, f64 b, f64 eps) {
    f64 d = a - b
    if d < 0.0 { d = 0.0 - d }
    if d > eps { fail(f"expect_near failed: |{a} - {b}| > {eps}") }
}
