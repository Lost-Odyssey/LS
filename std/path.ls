// std/path.ls — Path manipulation utilities.
// Pure LS byte operations over Str — no C backend, no platform conditionals.
// Supports both forward slashes (47) and backslashes (92) as separators.

import std.str

// Returns true if the byte at index i is a path separator.
fn _is_sep(&Str p, int i) -> bool {
    int ch = p.byte_at(i)
    return ch == 47 || ch == 92   /* '/' == 47, '\' == 92 */
}

// ---- Component extraction ----

// Return the final component of a path (everything after the last separator).
// "a/b/c" → "c",  "a/b/" → "",  "foo" → "foo"
fn basename(Str p) -> Str {
    int n = p.len()
    if n == 0 { return "" }
    int last = n - 1
    int sep = -1
    int i = last
    while i >= 0 {
        if _is_sep(p, i) {
            sep = i
            i = -1
        }
        i = i - 1
    }
    if sep == -1 { return p }
    if sep == last { return "" }
    return p.substr(sep + 1, last - sep)
}

// Return the directory component (everything before the last separator).
// "a/b/c" → "a/b",  "/foo" → "/",  "foo" → "."
fn dirname(Str p) -> Str {
    int n = p.len()
    if n == 0 { return "." }
    int last = n - 1
    /* Strip trailing separator(s) */
    while last > 0 && _is_sep(p, last) {
        last = last - 1
    }
    int sep = -1
    int i = last
    while i >= 0 {
        if _is_sep(p, i) {
            sep = i
            i = -1
        }
        i = i - 1
    }
    if sep == -1 { return "." }
    if sep == 0  { return p.substr(0, 1) }
    return p.substr(0, sep)
}

// Return the file extension including the dot, or "" if none.
// "file.txt" → ".txt",  "archive.tar.gz" → ".gz",  "Makefile" → ""
fn ext(Str p) -> Str {
    Str b = basename(p)
    int n = b.len()
    int i = n - 1
    while i > 0 {
        if b.byte_at(i) == 46 {   /* '.' == 46 */
            return b.substr(i, n - i)
        }
        i = i - 1
    }
    return ""
}

// Return the basename without its extension.
// "file.txt" → "file",  "archive.tar.gz" → "archive.tar",  "Makefile" → "Makefile"
fn stem(Str p) -> Str {
    Str b = basename(p)
    int n = b.len()
    int i = n - 1
    while i > 0 {
        if b.byte_at(i) == 46 {   /* '.' == 46 */
            return b.substr(0, i)
        }
        i = i - 1
    }
    return b
}

// ---- Path construction ----

// Join two path segments with a forward slash.
// Avoids double separators.
fn join(Str a, Str b) -> Str {
    int na = a.len()
    int nb = b.len()
    if na == 0 { return b }
    if nb == 0 { return a }
    bool a_has_sep = _is_sep(a, na - 1)
    bool b_has_sep = _is_sep(b, 0)
    if a_has_sep && b_has_sep {
        Str rest = b.substr(1, nb - 1)
        a.push_str(rest)
        return a
    }
    if a_has_sep || b_has_sep {
        a.push_str(b)
        return a
    }
    a.push_byte(47)   /* '/' */
    a.push_str(b)
    return a
}

// ---- Absolute path check ----

// Returns true if the path is absolute.
fn is_absolute(Str p) -> bool {
    int n = p.len()
    if n == 0 { return false }
    if _is_sep(p, 0) { return true }
    /* Windows drive letter: "X:/" or "X:\" */
    if n >= 3 {
        if p.byte_at(1) == 58 && _is_sep(p, 2) {   /* ':' == 58 */
            return true
        }
    }
    return false
}
