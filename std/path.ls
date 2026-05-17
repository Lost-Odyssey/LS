// std/path.ls — Path manipulation utilities.
// Pure LS string operations — no C backend, no platform conditionals.
// Supports both forward slashes (47) and backslashes (92) as separators.

// Returns true if the character at index i is a path separator.
fn _is_sep(string p, int i) -> bool {
    int ch = p.at(i)
    return ch == 47 || ch == 92   /* '/' == 47, '\' == 92 */
}

// ---- Component extraction ----

// Return the final component of a path (everything after the last separator).
// "a/b/c" → "c",  "a/b/" → "",  "foo" → "foo"
fn basename(string p) -> string {
    int n = p.length
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
    if sep == -1 { return p.copy() }
    if sep == last { return "" }
    return p.substr(sep + 1, last - sep)
}

// Return the directory component (everything before the last separator).
// "a/b/c" → "a/b",  "/foo" → "/",  "foo" → "."
fn dirname(string p) -> string {
    int n = p.length
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
fn ext(string p) -> string {
    string b = basename(p)
    int n = b.length
    int i = n - 1
    while i > 0 {
        if b.at(i) == 46 {   /* '.' == 46 */
            return b.substr(i, n - i)
        }
        i = i - 1
    }
    return ""
}

// Return the basename without its extension.
// "file.txt" → "file",  "archive.tar.gz" → "archive.tar",  "Makefile" → "Makefile"
fn stem(string p) -> string {
    string b = basename(p)
    int n = b.length
    int i = n - 1
    while i > 0 {
        if b.at(i) == 46 {   /* '.' == 46 */
            return b.substr(0, i)
        }
        i = i - 1
    }
    return b.copy()
}

// ---- Path construction ----

// Join two path segments with a forward slash.
// Avoids double separators.
fn join(string a, string b) -> string {
    int na = a.length
    int nb = b.length
    if na == 0 { return b.copy() }
    if nb == 0 { return a.copy() }
    bool a_has_sep = _is_sep(a, na - 1)
    bool b_has_sep = _is_sep(b, 0)
    string result = a.copy()
    if a_has_sep && b_has_sep {
        result.append(b.substr(1, nb - 1))
        return result
    }
    if a_has_sep || b_has_sep {
        result.append(b)
        return result
    }
    result.append("/")
    result.append(b)
    return result
}

// ---- Absolute path check ----

// Returns true if the path is absolute.
fn is_absolute(string p) -> bool {
    int n = p.length
    if n == 0 { return false }
    if _is_sep(p, 0) { return true }
    /* Windows drive letter: "X:/" or "X:\" */
    if n >= 3 {
        if p.at(1) == 58 && _is_sep(p, 2) {   /* ':' == 58 */
            return true
        }
    }
    return false
}
