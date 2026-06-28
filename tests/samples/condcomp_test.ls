// Phase E.3.2 — conditional compilation #if WINDOWS / LINUX / MACOS
//
// Verifies the scanner correctly suppresses tokens in inactive branches.
// Each test relies on the inactive branch containing code that would
// produce a parse / type error if compiled.

import std.core.str

extern def strlen(*u8 s) -> i64

#if WINDOWS
def platform_name() -> Str {
    return "WINDOWS"
}
#else
// This branch must be skipped on Windows. If the scanner failed to
// suppress it, the parser would see a duplicate def definition (or hit
// the bogus identifier below) and fail.
def platform_name() -> Str {
    return "OTHER"
}
this_should_be_skipped_garbage_!!!
#end

// Test 2: #if without #else
// On Linux: this block is active (def defined, clean body).
// On Windows/macOS: this block is inactive (def not defined).
#if LINUX
def _linux_only() -> int { return 1 }
#end

// On Windows: this block is active (def defined, clean body).
// On Linux/macOS: this block is inactive (def not defined).
#if WINDOWS
def _win_only() -> int { return 1 }
#end

// Test 2b: garbage in always-inactive block (MACOS on Win/Linux test environments).
// Verifies the scanner correctly suppresses unparseable tokens in inactive branches.
#if MACOS
garbage_that_must_be_skipped_and_never_parsed_!!!
def bad_fn() -> Str { return undefined_on_all_platforms() }
#end

// Test 3: nested conditional (Windows-only: inner #else should be taken)
#if WINDOWS
def outer_win() -> int {
    #if MACOS
    return mac_only_call()
    #else
    return 42
    #end
}
#end

def main() {
    Str p = platform_name()
    if strlen(p.c_str()) <= 0 {
        @print("FAIL: platform_name returned empty")
        return
    }
    @print("PASS: conditional fn definition (active branch)")
    @print(p)

    // Test 3: nested #if — outer_win() only exists on Windows
    #if WINDOWS
    int r = outer_win()
    if r != 42 {
        @print("FAIL: nested conditional returned ")
        @print(r)
        return
    }
    @print("PASS: nested #if (inner #else taken)")
    #end

    @print("ALL PASS")
}
