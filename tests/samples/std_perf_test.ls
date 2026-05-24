import std.perf as p

fn main() {
    int pass = 0
    int fail = 0

    // ── now() returns i64 > 0 ────────────────────────────────────────────────
    i64 t0 = p.now()
    if t0 > 0 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: now() <= 0") }

    // ── rdtsc() returns i64 > 0 ──────────────────────────────────────────────
    i64 tsc = p.rdtsc()
    if tsc > 0 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: rdtsc() <= 0") }

    // ── rdtscp() returns i64 > 0 ─────────────────────────────────────────────
    i64 cyc = p.rdtscp()
    if cyc > 0 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: rdtscp() <= 0") }

    // ── elapsed_ns returns i64 >= 0 ──────────────────────────────────────────
    i64 ns = p.elapsed_ns(t0)
    if ns >= 0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: elapsed_ns={ns}") }

    // ── elapsed_ms returns f64 >= 0 ──────────────────────────────────────────
    f64 ms = p.elapsed_ms(t0)
    if ms >= 0.0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: elapsed_ms={ms}") }

    // ── elapsed_s returns f64 >= 0 ───────────────────────────────────────────
    f64 s = p.elapsed_s(t0)
    if s >= 0.0 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL: elapsed_s={s}") }

    // ── monotonic: t1 >= t0 ───────────────────────────────────────────────────
    i64 t1 = p.now()
    if t1 >= t0 { pass = pass + 1 } else { fail = fail + 1; print("FAIL: not monotonic") }

    if fail == 0 {
        print("ALL PASS")
    } else {
        print(f"FAILED: {fail} tests")
    }
}
