// time_basic_test.ls — Basic tests for std.time module.
// Tests: unix timestamps, duration arithmetic, now_local/now_utc field ranges,
// format, parse, diff_s, add, sleep_ms, iso8601.
import std.time as T
import std.str

fn main() {
    // ---- Unix timestamp sanity ----
    i64 ns = T.now_unix_ns()
    if ns > 0 { print("PASS: now_unix_ns") } else { print("FAIL: now_unix_ns") }

    i64 ms = T.now_unix_ms()
    if ms > 0 { print("PASS: now_unix_ms") } else { print("FAIL: now_unix_ms") }

    f64 s = T.now_unix_s()
    if s > 0.0 { print("PASS: now_unix_s") } else { print("FAIL: now_unix_s") }

    // ---- Duration constructors ----
    i64 d_ns = T.duration_ns(42)
    i64 exp_ns = 42
    if d_ns == exp_ns { print("PASS: duration_ns") } else { print("FAIL: duration_ns") }

    i64 d_us = T.duration_us(1000)
    i64 exp_us = 1000000
    if d_us == exp_us { print("PASS: duration_us") } else { print("FAIL: duration_us") }

    i64 d_ms = T.duration_ms(500)
    i64 exp_ms = 500000000
    if d_ms == exp_ms { print("PASS: duration_ms") } else { print("FAIL: duration_ms") }

    i64 d_s = T.duration_s(3)
    i64 exp_s_base = 1000000000
    i64 exp_s = exp_s_base * 3
    if d_s == exp_s { print("PASS: duration_s") } else { print("FAIL: duration_s") }

    // ---- now_local: field range checks ----
    DateTime loc = T.now_local()
    if loc.year >= 2025 { print("PASS: local year") } else { print("FAIL: local year") }
    if loc.month >= 1 && loc.month <= 12 { print("PASS: local month") } else { print("FAIL: local month") }
    if loc.day >= 1 && loc.day <= 31 { print("PASS: local day") } else { print("FAIL: local day") }
    if loc.hour >= 0 && loc.hour <= 23 { print("PASS: local hour") } else { print("FAIL: local hour") }
    if loc.minute >= 0 && loc.minute <= 59 { print("PASS: local minute") } else { print("FAIL: local minute") }
    if loc.second >= 0 && loc.second <= 60 { print("PASS: local second") } else { print("FAIL: local second") }
    if loc.weekday >= 0 && loc.weekday <= 6 { print("PASS: local weekday") } else { print("FAIL: local weekday") }

    // ---- now_utc: utcoff must be 0 ----
    DateTime utc = T.now_utc()
    if utc.utcoff == 0 { print("PASS: utc utcoff") } else { print("FAIL: utc utcoff") }
    if utc.year >= 2025 { print("PASS: utc year") } else { print("FAIL: utc year") }

    // ---- format ----
    Str fmt_out = T.format(utc, "%Y-%m-%d")
    if fmt_out != "" { print("PASS: format") } else { print("FAIL: format") }

    // ---- iso8601 ----
    Str iso = T.iso8601(utc)
    if iso != "" { print("PASS: iso8601") } else { print("FAIL: iso8601") }

    // ---- add + diff_s ----
    DateTime utc2 = T.add(utc, T.duration_s(60))
    i64 diff = T.diff_s(utc, utc2)
    i64 exp_diff = 60
    if diff == exp_diff { print("PASS: diff_s") } else { print("FAIL: diff_s") }

    // ---- parse ----
    Result(DateTime, Str) pr = T.parse("2026-05-17", "%Y-%m-%d")
    match pr {
        Ok(pdt) => {
            if pdt.year == 2026 { print("PASS: parse year") } else { print("FAIL: parse year") }
            if pdt.month == 5 { print("PASS: parse month") } else { print("FAIL: parse month") }
            if pdt.day == 17 { print("PASS: parse day") } else { print("FAIL: parse day") }
        }
        Err(e) => { print("FAIL: parse") }
    }

    // ---- sleep (brief) ----
    T.sleep_ms(1)
    print("PASS: sleep_ms")

    print("ALL PASS")
}
