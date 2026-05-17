// std/time.ls — Calendar and duration utilities for LS.
// Pure LS — platform differences handled by std.os (os_win32.c / os_posix.c).

import std.os as _os

// ---- Public types ----

// Broken-down calendar time.
//   weekday: 0=Mon..6=Sun (ISO 8601)
//   yday:    0-365 (day of year)
//   utcoff:  UTC offset in seconds (positive = east, 0 for UTC datetimes)
//   unix_s:  Unix timestamp (seconds since 1970-01-01 UTC); used for arithmetic
struct DateTime {
    int year
    int month
    int day
    int hour
    int minute
    int second
    int weekday
    int yday
    int utcoff
    i64 unix_s
}

// ---- Internal helpers ----

fn _query_dt(i64 unix_s) -> DateTime {
    int yr  = _os.raw_time_get_year()
    int mo  = _os.raw_time_get_month()
    int dy  = _os.raw_time_get_day()
    int hr  = _os.raw_time_get_hour()
    int mn  = _os.raw_time_get_minute()
    int sc  = _os.raw_time_get_second()
    int wd  = _os.raw_time_get_weekday()
    int yd  = _os.raw_time_get_yday()
    int off = _os.raw_time_get_utcoff()
    DateTime dt = DateTime {
        year: yr, month: mo, day: dy,
        hour: hr, minute: mn, second: sc,
        weekday: wd, yday: yd, utcoff: off,
        unix_s: unix_s
    }
    return dt
}

// ---- Current time ----

fn now_unix_ns() -> i64 {
    return _os.raw_time_now_unix_ns()
}

fn now_unix_ms() -> i64 {
    return _os.raw_time_now_unix_ms()
}

fn now_unix_s() -> f64 {
    i64 ns = _os.raw_time_now_unix_ns()
    return (ns as f64) / 1000000000.0
}

fn now_local() -> DateTime {
    i64 ms  = _os.raw_time_now_unix_ms()
    i64 s   = ms / 1000
    _os.raw_time_from_unix_local(s)
    return _query_dt(s)
}

fn now_utc() -> DateTime {
    i64 ms = _os.raw_time_now_unix_ms()
    i64 s  = ms / 1000
    _os.raw_time_from_unix_utc(s)
    return _query_dt(s)
}

// ---- Formatting ----

fn format(DateTime dt, string fmt) -> string {
    object r = _os.raw_time_format(dt.year, dt.month, dt.day,
                                    dt.hour, dt.minute, dt.second,
                                    dt.weekday, dt.yday, fmt)
    return from_cstr(r)
}

// Returns an ISO 8601 string: "2026-05-16T10:30:00+08:00"
fn iso8601(DateTime dt) -> string {
    string date_part = format(dt, "%Y-%m-%dT%H:%M:%S")
    int off = dt.utcoff
    if off == 0 {
        return date_part + "Z"
    }
    string sign = "+"
    if off < 0 {
        sign = "-"
        off = 0 - off
    }
    int hh = off / 3600
    int mm = (off % 3600) / 60
    string hh_s = ""
    string mm_s = ""
    if hh < 10 { hh_s = f"0{hh}" } else { hh_s = f"{hh}" }
    if mm < 10 { mm_s = f"0{mm}" } else { mm_s = f"{mm}" }
    return date_part + sign + hh_s + ":" + mm_s
}

// ---- Parsing ----

// Parses text using strftime-style format.
// Supported specifiers: %Y %m %d %H %M %S (and literal separators).
// Returns Ok(DateTime) or Err(string).
fn parse(string text, string fmt) -> Result(DateTime, string) {
    int ok = _os.raw_time_parse(text, fmt)
    if ok == 0 {
        return Err(f"time.parse: cannot parse '{text}' with format '{fmt}'")
    }
    int yr = _os.raw_time_get_year()
    int mo = _os.raw_time_get_month()
    int dy = _os.raw_time_get_day()
    int hr = _os.raw_time_get_hour()
    int mn = _os.raw_time_get_minute()
    int sc = _os.raw_time_get_second()
    i64 unix_s = _os.raw_time_to_unix(yr, mo, dy, hr, mn, sc, 1)
    _os.raw_time_from_unix_utc(unix_s)
    DateTime dt = _query_dt(unix_s)
    return Ok(dt)
}

// ---- Duration arithmetic ----

// Construct a Duration (nanoseconds i64) from various units.
fn duration_ns(i64 ns) -> i64 { return ns }
fn duration_us(i64 us) -> i64 { return us * 1000 }
fn duration_ms(i64 ms) -> i64 { return ms * 1000000 }
fn duration_s(i64 s)   -> i64 { return s  * 1000000000 }

// Add a duration (nanoseconds) to a DateTime, returning a new UTC DateTime.
fn add(DateTime dt, i64 dur_ns) -> DateTime {
    i64 new_s = dt.unix_s + dur_ns / 1000000000
    _os.raw_time_from_unix_utc(new_s)
    return _query_dt(new_s)
}

// Signed difference in seconds: dt2.unix_s - dt1.unix_s.
fn diff_s(DateTime dt1, DateTime dt2) -> i64 {
    return dt2.unix_s - dt1.unix_s
}

// ---- Sleep ----

fn sleep_ms(i64 ms) { _os.raw_sleep_ms(ms) }
fn sleep_us(i64 us) { _os.raw_sleep_us(us) }
