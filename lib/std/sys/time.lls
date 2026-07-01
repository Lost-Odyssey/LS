// std/time.ls — Calendar and duration utilities for LS.
// Pure LS — platform differences handled by std.sys.os (os_win32.c / os_posix.c).

import std.sys.os as _os
import std.core.str

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

def _query_dt(i64 unix_s) -> DateTime {
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

def now_unix_ns() -> i64 {
    return _os.raw_time_now_unix_ns()
}

def now_unix_ms() -> i64 {
    return _os.raw_time_now_unix_ms()
}

def now_unix_s() -> f64 {
    i64 ns = _os.raw_time_now_unix_ns()
    return (ns as f64) / 1000000000.0
}

def now_local() -> DateTime {
    i64 ms  = _os.raw_time_now_unix_ms()
    i64 s   = ms / 1000
    _os.raw_time_from_unix_local(s)
    return _query_dt(s)
}

def now_utc() -> DateTime {
    i64 ms = _os.raw_time_now_unix_ms()
    i64 s  = ms / 1000
    _os.raw_time_from_unix_utc(s)
    return _query_dt(s)
}

// ---- Formatting ----

def format(DateTime dt, Str fmt) -> Str {
    object r = _os.raw_time_format(dt.year, dt.month, dt.day,
                                    dt.hour, dt.minute, dt.second,
                                    dt.weekday, dt.yday, fmt)
    Str s = from_cstr(r)
    return s
}

// Returns an ISO 8601 string: "2026-05-16T10:30:00+08:00"
def iso8601(DateTime dt) -> Str {
    Str out = format(dt, "%Y-%m-%dT%H:%M:%S")
    int off = dt.utcoff
    if off == 0 {
        Str z = "Z"
        out.push_str(z)
        return out
    }
    int o = off
    if off < 0 {
        o = 0 - off
        out.push_byte(45)   // '-'
    } else {
        out.push_byte(43)   // '+'
    }
    int hh = o / 3600
    int mm = (o % 3600) / 60
    Str hh_s = f"{hh}"
    Str mm_s = f"{mm}"
    Str zero = "0"
    if hh < 10 { out.push_str(zero) }
    out.push_str(hh_s)
    out.push_byte(58)       // ':'
    if mm < 10 { out.push_str(zero) }
    out.push_str(mm_s)
    return out
}

// ---- Parsing ----

// Parses text using strftime-style format.
// Supported specifiers: %Y %m %d %H %M %S (and literal separators).
// Returns Ok(DateTime) or Err(Str).
def parse(Str text, Str fmt) -> Result(DateTime, Str) {
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
def duration_ns(i64 ns) -> i64 { return ns }
def duration_us(i64 us) -> i64 { return us * 1000 }
def duration_ms(i64 ms) -> i64 { return ms * 1000000 }
def duration_s(i64 s)   -> i64 { return s  * 1000000000 }

// Add a duration (nanoseconds) to a DateTime, returning a new UTC DateTime.
def add(DateTime dt, i64 dur_ns) -> DateTime {
    i64 new_s = dt.unix_s + dur_ns / 1000000000
    _os.raw_time_from_unix_utc(new_s)
    return _query_dt(new_s)
}

// Signed difference in seconds: dt2.unix_s - dt1.unix_s.
def diff_s(DateTime dt1, DateTime dt2) -> i64 {
    return dt2.unix_s - dt1.unix_s
}

// ---- Sleep ----

def sleep_ms(i64 ms) { _os.raw_sleep_ms(ms) }
def sleep_us(i64 us) { _os.raw_sleep_us(us) }

def now() -> Str { return iso8601(now_utc())}