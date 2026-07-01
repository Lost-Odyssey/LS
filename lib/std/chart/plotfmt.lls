// std/plotfmt.ls — Pure-LS formatting helpers for the plot module (and general use).
//
// Wraps strconv.float_fixed and math intrinsics to provide the numeric/string
// formatting that plot needs without relying on f-string format specifiers
// (which LS does not yet support). Zero compiler changes; pure LS.
//
//   fmt_fixed(v, d)   — v with exactly d decimal places
//   fmt_auto(v)       — auto decimal count by magnitude
//   fmt_sci(v)        — scientific notation "1.23e4"
//   fmt_time(ns)      — auto unit ns/us/ms/s
//   pad_left/right    — space padding for alignment
//   clamp_i/clamp_f   — clamp to range (f64 min/max use math.min/max directly)
//   rgb_to_hex        — (r,g,b) 0..255 -> "#rrggbb"
//   hsv_to_hex        — HSV (0..360, 0..1, 0..1) -> "#rrggbb"

import std.core.math as math
import std.text.strconv as strconv
import std.core.str

// ---- fmt_fixed(v, d) -> Str ----
// v formatted with exactly d decimal places (printf rounding).
def fmt_fixed(f64 v, int d) -> Str {
    return strconv.float_fixed(v, d)
}

// ---- fmt_auto(v) -> Str ----
// Picks a decimal count based on magnitude — good default for axis tick labels.
def fmt_auto(f64 v) -> Str {
    f64 a = math.abs(v)
    if a == 0.0 { return "0" }
    if a >= 1000.0 { return strconv.float_fixed(v, 0) }
    if a >= 10.0 { return strconv.float_fixed(v, 1) }
    if a >= 0.1 { return strconv.float_fixed(v, 2) }
    if a >= 0.001 { return strconv.float_fixed(v, 4) }
    return strconv.float_fixed(v, 6)
}

// ---- fmt_sci(v) -> Str ----
// Scientific notation with 2-digit mantissa, e.g. 12345.0 -> "1.23e4".
def fmt_sci(f64 v) -> Str {
    if v == 0.0 { return "0e0" }
    f64 a = math.abs(v)
    f64 expf = math.floor(math.log10(a))
    f64 mant = v / math.pow(10.0, expf)
    int e = expf as int
    return strconv.float_fixed(mant, 2) + "e" + strconv.to_string(e)
}

// ---- fmt_time(ns) -> Str ----
// Formats a nanosecond duration with an auto-selected unit.
def fmt_time(i64 ns) -> Str {
    i64 a = ns
    if a < 0 { a = 0 - a }
    if a >= 1000000000 {
        f64 val = (ns as f64) / 1000000000.0
        return strconv.float_fixed(val, 2) + "s"
    }
    if a >= 1000000 {
        f64 val = (ns as f64) / 1000000.0
        return strconv.float_fixed(val, 1) + "ms"
    }
    if a >= 1000 {
        f64 val = (ns as f64) / 1000.0
        return strconv.float_fixed(val, 0) + "us"
    }
    Str s = f"{ns}ns"
    return s
}

// ---- pad_left / pad_right ----
// Pad s with spaces to width w (no truncation if already wider).
// (Str.pad_left/pad_right pad with an arbitrary fill byte; these keep the
// historical space-padding signature.)
def pad_left(Str s, int w) -> Str {
    return s.pad_left(w, 32)    /* ' ' */
}

def pad_right(Str s, int w) -> Str {
    return s.pad_right(w, 32)   /* ' ' */
}

// ---- clamp ----
def clamp_i(int v, int lo, int hi) -> int {
    if v < lo { return lo }
    if v > hi { return hi }
    return v
}

def clamp_f(f64 v, f64 lo, f64 hi) -> f64 {
    if v < lo { return lo }
    if v > hi { return hi }
    return v
}

// ---- color helpers ----

// Two-digit lowercase hex for a byte 0..255.
def _hex2(int n) -> Str {
    int c = clamp_i(n, 0, 255)
    Str h = strconv.int_to_hex(c)
    if h.len() < 2 {
        /* a bare "0" literal on the LHS of `+` would still be a builtin
           string (no expected type) — start from a Str local instead */
        Str z = "0"
        return z + h
    }
    return h
}

// (r,g,b) each 0..255 -> "#rrggbb".
def rgb_to_hex(int r, int g, int b) -> Str {
    Str out = "#"
    return out + _hex2(r) + _hex2(g) + _hex2(b)
}

// HSV -> "#rrggbb". h in degrees [0,360), s,v in [0,1].
def hsv_to_hex(f64 h, f64 s, f64 v) -> Str {
    // normalize hue into [0,360)
    f64 hh = h - 360.0 * math.floor(h / 360.0)
    f64 c = v * s
    f64 hp = hh / 60.0
    // x = c * (1 - |hp mod 2 - 1|), avoid float % operator
    f64 hpmod2 = hp - 2.0 * math.floor(hp / 2.0)
    f64 x = c * (1.0 - math.abs(hpmod2 - 1.0))
    f64 m = v - c
    f64 r1 = 0.0
    f64 g1 = 0.0
    f64 b1 = 0.0
    int sext = hp as int
    if sext == 0 { r1 = c; g1 = x; b1 = 0.0 }
    else if sext == 1 { r1 = x; g1 = c; b1 = 0.0 }
    else if sext == 2 { r1 = 0.0; g1 = c; b1 = x }
    else if sext == 3 { r1 = 0.0; g1 = x; b1 = c }
    else if sext == 4 { r1 = x; g1 = 0.0; b1 = c }
    else { r1 = c; g1 = 0.0; b1 = x }
    int r = ((r1 + m) * 255.0 + 0.5) as int
    int g = ((g1 + m) * 255.0 + 0.5) as int
    int b = ((b1 + m) * 255.0 + 0.5) as int
    return rgb_to_hex(r, g, b)
}
