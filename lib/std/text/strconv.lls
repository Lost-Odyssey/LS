// std/strconv.ls — String formatting and number-base conversion utilities.
// Pure LS module; float_fixed uses two thin C helpers in runtime/builtins.c.

import std.core.vec
import std.core.str

// ---- C helpers for float formatting ----
// __ls_float_fixed_exec formats val into a global static buffer (no malloc).
// __ls_float_fixed_ptr returns a pointer to that buffer; caller must copy
// (e.g. via from_cstr) before calling again.

extern def __ls_float_fixed_exec(f64 val, int digits)
extern def __ls_float_fixed_ptr() -> object

// ---- format(fmt, Vec(Str) args) -> Str ----
// Replaces each "{}" placeholder in fmt with successive elements of args.
// Extra "{}" beyond args.len() are replaced with empty string; extra args are ignored.

def format(Str fmt, Vec(Str) args) -> Str {
    Str result = ""
    int arg_idx = 0
    int i = 0
    int n = fmt.len()
    int seg_start = 0
    while i < n {
        // 123 = '{', 125 = '}'
        if i + 1 < n && fmt.byte_at!(i) == 123 && fmt.byte_at!(i + 1) == 125 {
            if i > seg_start {
                Str seg = fmt.substr(seg_start, i - seg_start)
                result.push_str(seg)
            }
            if arg_idx < args.len() {
                Str a = args[arg_idx]
                result.push_str(a)
                arg_idx = arg_idx + 1
            }
            i = i + 2
            seg_start = i
        } else {
            i = i + 1
        }
    }
    if seg_start < n {
        Str tail = fmt.substr(seg_start, n - seg_start)
        result.push_str(tail)
    }
    return result
}

// ---- base conversion core ----
// Emits |n| in the given base (digits 0-9 then a-f), reversed-then-flipped.

def _to_base(int n, int base) -> Str {
    if n == 0 { return "0" }
    Str rev = ""
    bool neg = n < 0
    if neg { n = 0 - n }
    while n > 0 {
        int d = n % base
        int ch = 48 + d            // '0' + d
        if d >= 10 { ch = 87 + d } // 'a' + (d - 10)
        rev.push_byte(ch)
        n = n / base
    }
    Str out = ""
    if neg { out.push_byte(45) }   // '-'
    for (int i = rev.len() - 1; i >= 0; i = i - 1) {
        out.push_byte(rev.byte_at!(i))
    }
    return out
}

// ---- int_to_hex(int) -> Str ----
// Lowercase hexadecimal (no "0x" prefix); negatives prefixed with "-".
def int_to_hex(int n) -> Str { return _to_base(n, 16) }

// ---- int_to_oct(int) -> Str ----
def int_to_oct(int n) -> Str { return _to_base(n, 8) }

// ---- int_to_bin(int) -> Str ----
def int_to_bin(int n) -> Str { return _to_base(n, 2) }

// ---- float_fixed(f64 n, int digits) -> Str ----
// Formats n with exactly `digits` decimal places (rounded by printf rules).
// Example: float_fixed(3.14159, 2) -> "3.14"

def float_fixed(f64 n, int digits) -> Str {
    __ls_float_fixed_exec(n, digits)
    object ptr = __ls_float_fixed_ptr()
    Str s = from_cstr(ptr)
    return s
}

// ---- to_string / to_string_f ----
// Convenience wrappers for converting numbers to their default Str form.

def to_string(int n) -> Str {
    Str s = f"{n}"
    return s
}

def to_string_f(f64 n) -> Str {
    Str s = f"{n}"
    return s
}
