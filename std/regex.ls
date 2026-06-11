// std/regex.ls — LS regular expression stdlib (Pike VM NFA, pure C backend)
//
// Import:  import std.regex as re
// All functions return false/empty/0 on invalid pattern (no crash).
//
// Flags passed to compile:
//   LS_RE_IGNORECASE = 1  (also activated by (?i) inside pattern)
//   LS_RE_MULTILINE  = 2  (also activated by (?m))
//   LS_RE_DOTALL     = 4  (also activated by (?s))
//
//   capture()     -> Vec(Str)        — empty = no match
//   capture_all() -> Vec(Str)        — flat: all groups from all matches
//   capture_named() -> Map(Str,Str)  — empty = no match
// Use group_count(pattern) to get the per-match stride for capture_all.
//
// string->Str migration note: the public API takes/returns Str, but the
// internals keep a builtin-string working copy (`string st = text` — the
// var-decl bridge copies + NUL-terminates) because the C regex engine is a
// direct extern (char* needs the NUL; the call-arg bridge does not cover
// direct extern calls). Collected results go back out through the
// string->Str bridges (push / set / return).

import std.vec
import std.map
import std.c as c
import std.str

// ---- internal helpers (builtin-string domain) ----

fn _compile(string pattern, int flags) -> int {
    return c.__ls_regex_compile(pattern, flags)
}

fn _cap_str(string text, int group) -> string {
    int s = c.__ls_regex_cap_start(group)
    int l = c.__ls_regex_cap_len(group)
    if s < 0 { return "" }
    return text.substr(s, l)
}

// ---- 3.1 Basic matching ----

// Returns true if pattern appears anywhere in text.
fn matches(Str text, Str pattern) -> bool {
    string st = text
    string sp = pattern
    int h = _compile(sp, 0)
    if h < 0 { return false }
    int n = c.__ls_regex_exec(h, st, st.length, 0)
    c.__ls_regex_free(h)
    return n > 0
}

// Returns true if the entire text matches pattern (anchored both ends).
fn full_match(Str text, Str pattern) -> bool {
    string st = text
    string sp = pattern
    string anchored = "\\A(?:" + sp + ")\\Z"
    int h = _compile(anchored, 0)
    if h < 0 { return false }
    int n = c.__ls_regex_exec(h, st, st.length, 0)
    c.__ls_regex_free(h)
    return n > 0
}

// ---- 3.2 Find ----

// Returns Some(first_match) or None.
fn find(Str text, Str pattern) -> Option(Str) {
    string st = text
    string sp = pattern
    int h = _compile(sp, 0)
    if h < 0 { return None }
    int n = c.__ls_regex_exec(h, st, st.length, 0)
    if n == 0 { c.__ls_regex_free(h); return None }
    string m = _cap_str(st, 0)
    c.__ls_regex_free(h)
    Str ms = m
    return Some(ms)
}

// Returns all non-overlapping full matches.
fn find_all(Str text, Str pattern) -> Vec(Str) {
    Vec(Str) result = {}
    string st = text
    string sp = pattern
    int h = _compile(sp, 0)
    if h < 0 { return result }
    int pos = 0
    int tlen = st.length
    while pos <= tlen {
        int n = c.__ls_regex_exec(h, st, tlen, pos)
        if n == 0 { break }
        int s = c.__ls_regex_cap_start(0)
        int l = c.__ls_regex_cap_len(0)
        string piece = st.substr(s, l)
        result.push(piece)
        if l == 0 { pos = s + 1 } else { pos = s + l }
    }
    c.__ls_regex_free(h)
    return result
}

// ---- 3.3 Numbered capture groups ----

// Returns [full_match, group1, group2, ...] for the first match, or empty Vec.
fn capture(Str text, Str pattern) -> Vec(Str) {
    Vec(Str) caps = {}
    string st = text
    string sp = pattern
    int h = _compile(sp, 0)
    if h < 0 { return caps }
    int n = c.__ls_regex_exec(h, st, st.length, 0)
    if n == 0 { c.__ls_regex_free(h); return caps }
    int i = 0
    while i < n {
        string g = _cap_str(st, i)
        caps.push(g)
        i = i + 1
    }
    c.__ls_regex_free(h)
    return caps
}

// Returns the number of capture groups in the pattern (not counting group 0).
fn group_count(Str pattern) -> int {
    string sp = pattern
    int h = _compile(sp, 0)
    if h < 0 { return 0 }
    int n = c.__ls_regex_group_count(h)
    c.__ls_regex_free(h)
    return n
}

// Returns all matches with all groups, packed flat into a single Vec.
// Layout: [g0_m0, g1_m0, ..., gN_m0, g0_m1, g1_m1, ..., gN_m1, ...]
// Stride = group_count(pattern) + 1.  Returns empty Vec if no matches.
fn capture_all(Str text, Str pattern) -> Vec(Str) {
    Vec(Str) result = {}
    string st = text
    string sp = pattern
    int h = _compile(sp, 0)
    if h < 0 { return result }
    int pos = 0
    int tlen = st.length
    while pos <= tlen {
        int n = c.__ls_regex_exec(h, st, tlen, pos)
        if n == 0 { break }
        int s = c.__ls_regex_cap_start(0)
        int l = c.__ls_regex_cap_len(0)
        int i = 0
        while i < n {
            string g = _cap_str(st, i)
            result.push(g)
            i = i + 1
        }
        if l == 0 { pos = s + 1 } else { pos = s + l }
    }
    c.__ls_regex_free(h)
    return result
}

// ---- 3.4 Named capture groups ----

// Returns a map {name -> value} for named groups in the first match.
// Returns an empty map if no match or no named groups.
fn capture_named(Str text, Str pattern) -> Map(Str, Str) {
    Map(Str, Str) m = {}
    string st = text
    string sp = pattern
    int h = _compile(sp, 0)
    if h < 0 { return m }
    int n = c.__ls_regex_exec(h, st, st.length, 0)
    if n == 0 { c.__ls_regex_free(h); return m }
    int nc = c.__ls_regex_named_count(h)
    int i = 0
    while i < nc {
        string name = from_cstr(c.__ls_regex_named_name(h, i))
        int idx = c.__ls_regex_named_index(h, i)
        int s = c.__ls_regex_cap_start(idx)
        int l = c.__ls_regex_cap_len(idx)
        if s >= 0 {
            string val = st.substr(s, l)
            m.set(name, val)
        }
        i = i + 1
    }
    c.__ls_regex_free(h)
    return m
}

// ---- 3.5 Replace ----

// Replaces the first match of pattern with replacement.
fn replace(Str text, Str pattern, Str replacement) -> Str {
    string st = text
    string sp = pattern
    string sr = replacement
    int h = _compile(sp, 0)
    if h < 0 { return text }
    int n = c.__ls_regex_exec(h, st, st.length, 0)
    if n == 0 { c.__ls_regex_free(h); return text }
    int s = c.__ls_regex_cap_start(0)
    int l = c.__ls_regex_cap_len(0)
    string result = st.substr(0, s) + sr + st.substr(s + l, st.length - s - l)
    c.__ls_regex_free(h)
    return result
}

// Replaces all non-overlapping matches of pattern with replacement.
fn replace_all(Str text, Str pattern, Str replacement) -> Str {
    string st = text
    string sp = pattern
    string sr = replacement
    int h = _compile(sp, 0)
    if h < 0 { return text }
    string result = ""
    int pos = 0
    int tlen = st.length
    while pos <= tlen {
        int n = c.__ls_regex_exec(h, st, tlen, pos)
        if n == 0 { break }
        int s = c.__ls_regex_cap_start(0)
        int l = c.__ls_regex_cap_len(0)
        result = result + st.substr(pos, s - pos) + sr
        if l == 0 { result = result + st.substr(s, 1); pos = s + 1 }
        else { pos = s + l }
    }
    result = result + st.substr(pos, tlen - pos)
    c.__ls_regex_free(h)
    return result
}

// ---- 3.6 Split ----

// Splits text at each match of pattern; empty strings from consecutive
// separators are omitted.
fn split(Str text, Str pattern) -> Vec(Str) {
    Vec(Str) result = {}
    string st = text
    string sp = pattern
    int h = _compile(sp, 0)
    if h < 0 {
        /* st.copy() — `Str whole = st` would mark st maybe-moved on this
           branch (the B-1 var-decl bridge takes the string-binding move
           semantics), breaking the later uses on the main path. */
        Str whole = st.copy()
        result.push(whole)
        return result
    }
    int pos = 0
    int tlen = st.length
    while pos <= tlen {
        int n = c.__ls_regex_exec(h, st, tlen, pos)
        if n == 0 { break }
        int s = c.__ls_regex_cap_start(0)
        int l = c.__ls_regex_cap_len(0)
        string piece = st.substr(pos, s - pos)
        if piece.length > 0 { result.push(piece) }
        if l == 0 { pos = s + 1 } else { pos = s + l }
    }
    if pos <= tlen {
        string tail = st.substr(pos, tlen - pos)
        if tail.length > 0 { result.push(tail) }
    }
    c.__ls_regex_free(h)
    return result
}
