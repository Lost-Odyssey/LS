// std/strconv.ls — String formatting and number-base conversion utilities.
// Pure LS module; float_fixed uses two thin C helpers in runtime/builtins.c.

// ---- C helpers for float formatting ----
// __ls_float_fixed_exec formats val into a global static buffer (no malloc).
// __ls_float_fixed_ptr returns a pointer to that buffer; caller must copy
// (e.g. via from_cstr) before calling again.

extern fn __ls_float_fixed_exec(f64 val, int digits)
extern fn __ls_float_fixed_ptr() -> object

// ---- format(fmt, vec(string) args) -> string ----
// Replaces each "{}" placeholder in fmt with successive elements of args.
// Extra "{}" beyond args.length are replaced with empty string; extra args are ignored.

fn format(string fmt, vec(string) args) -> string {
    string result = ""
    int arg_idx = 0
    int i = 0
    int n = fmt.length
    int seg_start = 0
    while i < n {
        // 123 = '{', 125 = '}'
        if i + 1 < n && fmt.at(i) == 123 && fmt.at(i + 1) == 125 {
            if i > seg_start {
                result.append(fmt.substr(seg_start, i - seg_start))
            }
            if arg_idx < args.length {
                result.append(args[arg_idx])
                arg_idx = arg_idx + 1
            }
            i = i + 2
            seg_start = i
        } else {
            i = i + 1
        }
    }
    if seg_start < n {
        result.append(fmt.substr(seg_start, n - seg_start))
    }
    return result
}

// ---- int_to_hex(int) -> string ----
// Returns lowercase hexadecimal representation (no "0x" prefix).
// Negative numbers are prefixed with "-".

fn int_to_hex(int n) -> string {
    if n == 0 { return "0" }
    string hex_chars = "0123456789abcdef"
    string result = ""
    bool neg = n < 0
    if neg { n = 0 - n }
    while n > 0 {
        int d = n % 16
        string ch = hex_chars.substr(d, 1)
        result = ch + result
        n = n / 16
    }
    if neg { result = "-" + result }
    return result
}

// ---- int_to_oct(int) -> string ----
// Returns octal representation (no "0" prefix).

fn int_to_oct(int n) -> string {
    if n == 0 { return "0" }
    string result = ""
    bool neg = n < 0
    if neg { n = 0 - n }
    while n > 0 {
        int d = n % 8
        string ch = f"{d}"
        result = ch + result
        n = n / 8
    }
    if neg { result = "-" + result }
    return result
}

// ---- int_to_bin(int) -> string ----
// Returns binary representation (no "0b" prefix).

fn int_to_bin(int n) -> string {
    if n == 0 { return "0" }
    string result = ""
    bool neg = n < 0
    if neg { n = 0 - n }
    while n > 0 {
        int d = n % 2
        string ch = f"{d}"
        result = ch + result
        n = n / 2
    }
    if neg { result = "-" + result }
    return result
}

// ---- float_fixed(f64 n, int digits) -> string ----
// Formats n with exactly `digits` decimal places (rounded by printf rules).
// Example: float_fixed(3.14159, 2) -> "3.14"

fn float_fixed(f64 n, int digits) -> string {
    __ls_float_fixed_exec(n, digits)
    object ptr = __ls_float_fixed_ptr()
    return from_cstr(ptr)
}

// ---- to_string / to_string_f ----
// Convenience wrappers for converting numbers to their default string form.

fn to_string(int n) -> string {
    return f"{n}"
}

fn to_string_f(f64 n) -> string {
    return f"{n}"
}
