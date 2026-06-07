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
// All vec-returning functions now use Vec(string) (std.vec replacement).
//   capture()     -> Vec(string)        — empty = no match
//   capture_all() -> Vec(string)        — flat: all groups from all matches
//   capture_named() -> map(string,str)  — empty = no match
// Use group_count(pattern) to get the per-match stride for capture_all.

import std.vec
import std.c as c

// ---- internal helpers ----

fn _compile(string pattern, int flags) -> int {
    return c.__ls_regex_compile(pattern, flags)
}

fn _exec(int h, string text) -> int {
    return c.__ls_regex_exec(h, text, text.length, 0)
}

fn _exec_at(int h, string text, int start) -> int {
    return c.__ls_regex_exec(h, text, text.length, start)
}

fn _cap_str(string text, int group) -> string {
    int s = c.__ls_regex_cap_start(group)
    int l = c.__ls_regex_cap_len(group)
    if s < 0 { return "" }
    return text.substr(s, l)
}

// ---- 3.1 Basic matching ----

// Returns true if pattern appears anywhere in text.
fn matches(string text, string pattern) -> bool {
    int h = _compile(pattern, 0)
    if h < 0 { return false }
    int n = _exec(h, text)
    c.__ls_regex_free(h)
    return n > 0
}

// Returns true if the entire text matches pattern (anchored both ends).
fn full_match(string text, string pattern) -> bool {
    string anchored = "\\A(?:" + pattern + ")\\Z"
    int h = _compile(anchored, 0)
    if h < 0 { return false }
    int n = _exec(h, text)
    c.__ls_regex_free(h)
    return n > 0
}

// ---- 3.2 Find ----

// Returns Some(first_match) or None.
fn find(string text, string pattern) -> Option(string) {
    int h = _compile(pattern, 0)
    if h < 0 { return None }
    int n = _exec(h, text)
    if n == 0 { c.__ls_regex_free(h); return None }
    string m = _cap_str(text, 0)
    c.__ls_regex_free(h)
    return Some(m)
}

// Returns all non-overlapping full matches.
fn find_all(string text, string pattern) -> Vec(string) {
    Vec(string) result = {}
    int h = _compile(pattern, 0)
    if h < 0 { return result }
    int pos = 0
    int tlen = text.length
    while pos <= tlen {
        int n = c.__ls_regex_exec(h, text, tlen, pos)
        if n == 0 { break }
        int s = c.__ls_regex_cap_start(0)
        int l = c.__ls_regex_cap_len(0)
        result.push(text.substr(s, l))
        if l == 0 { pos = s + 1 } else { pos = s + l }
    }
    c.__ls_regex_free(h)
    return result
}

// ---- 3.3 Numbered capture groups ----

// Returns [full_match, group1, group2, ...] for the first match, or empty Vec.
fn capture(string text, string pattern) -> Vec(string) {
    Vec(string) caps = {}
    int h = _compile(pattern, 0)
    if h < 0 { return caps }
    int n = _exec(h, text)
    if n == 0 { c.__ls_regex_free(h); return caps }
    int i = 0
    while i < n {
        caps.push(_cap_str(text, i))
        i = i + 1
    }
    c.__ls_regex_free(h)
    return caps
}

// Returns the number of capture groups in the pattern (not counting group 0).
fn group_count(string pattern) -> int {
    int h = _compile(pattern, 0)
    if h < 0 { return 0 }
    int n = c.__ls_regex_group_count(h)
    c.__ls_regex_free(h)
    return n
}

// Returns all matches with all groups, packed flat into a single Vec.
// Layout: [g0_m0, g1_m0, ..., gN_m0, g0_m1, g1_m1, ..., gN_m1, ...]
// Stride = group_count(pattern) + 1.  Returns empty Vec if no matches.
fn capture_all(string text, string pattern) -> Vec(string) {
    Vec(string) result = {}
    int h = _compile(pattern, 0)
    if h < 0 { return result }
    int pos = 0
    int tlen = text.length
    while pos <= tlen {
        int n = c.__ls_regex_exec(h, text, tlen, pos)
        if n == 0 { break }
        int s = c.__ls_regex_cap_start(0)
        int l = c.__ls_regex_cap_len(0)
        int i = 0
        while i < n {
            result.push(_cap_str(text, i))
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
fn capture_named(string text, string pattern) -> map(string, string) {
    map(string, string) m = {}
    int h = _compile(pattern, 0)
    if h < 0 { return m }
    int n = _exec(h, text)
    if n == 0 { c.__ls_regex_free(h); return m }
    int nc = c.__ls_regex_named_count(h)
    int i = 0
    while i < nc {
        string name = from_cstr(c.__ls_regex_named_name(h, i))
        int idx = c.__ls_regex_named_index(h, i)
        int s = c.__ls_regex_cap_start(idx)
        int l = c.__ls_regex_cap_len(idx)
        if s >= 0 { m.set(name, text.substr(s, l)) }
        i = i + 1
    }
    c.__ls_regex_free(h)
    return m
}

// ---- 3.5 Replace ----

// Replaces the first match of pattern with replacement.
fn replace(string text, string pattern, string replacement) -> string {
    int h = _compile(pattern, 0)
    if h < 0 { return text.copy() }
    int n = _exec(h, text)
    if n == 0 { c.__ls_regex_free(h); return text.copy() }
    int s = c.__ls_regex_cap_start(0)
    int l = c.__ls_regex_cap_len(0)
    string result = text.substr(0, s) + replacement + text.substr(s + l, text.length - s - l)
    c.__ls_regex_free(h)
    return result
}

// Replaces all non-overlapping matches of pattern with replacement.
fn replace_all(string text, string pattern, string replacement) -> string {
    int h = _compile(pattern, 0)
    if h < 0 { return text.copy() }
    string result = ""
    int pos = 0
    int tlen = text.length
    while pos <= tlen {
        int n = c.__ls_regex_exec(h, text, tlen, pos)
        if n == 0 { break }
        int s = c.__ls_regex_cap_start(0)
        int l = c.__ls_regex_cap_len(0)
        result = result + text.substr(pos, s - pos) + replacement
        if l == 0 { result = result + text.substr(s, 1); pos = s + 1 }
        else { pos = s + l }
    }
    result = result + text.substr(pos, tlen - pos)
    c.__ls_regex_free(h)
    return result
}

// ---- 3.6 Split ----

// Splits text at each match of pattern; empty strings from consecutive
// separators are omitted.
fn split(string text, string pattern) -> Vec(string) {
    Vec(string) result = {}
    int h = _compile(pattern, 0)
    if h < 0 { result.push(text.copy()); return result }
    int pos = 0
    int tlen = text.length
    while pos <= tlen {
        int n = c.__ls_regex_exec(h, text, tlen, pos)
        if n == 0 { break }
        int s = c.__ls_regex_cap_start(0)
        int l = c.__ls_regex_cap_len(0)
        string piece = text.substr(pos, s - pos)
        if piece.length > 0 { result.push(piece) }
        if l == 0 { pos = s + 1 } else { pos = s + l }
    }
    if pos <= tlen {
        string tail = text.substr(pos, tlen - pos)
        if tail.length > 0 { result.push(tail) }
    }
    c.__ls_regex_free(h)
    return result
}
