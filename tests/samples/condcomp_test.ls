// Phase E.3.2 — conditional compilation #if WINDOWS / LINUX / MACOS
//
// Verifies the scanner correctly suppresses tokens in inactive branches.
// Each test relies on the inactive branch containing code that would
// produce a parse / type error if compiled.

extern fn strlen(string s) -> i64

#if WINDOWS
fn platform_name() -> string {
    return "WINDOWS"
}
#else
// This branch must be skipped on Windows. If the scanner failed to
// suppress it, the parser would see a duplicate fn definition (or hit
// the bogus identifier below) and fail.
fn platform_name() -> string {
    return "OTHER"
}
this_should_be_skipped_garbage_!!!
#end

// Test 2: #if without #else
#if LINUX
fn _linux_only() -> int { return 1 }
some_garbage_only_seen_on_linux_path
#end

// Test 3: nested
#if WINDOWS
fn outer_win() -> int {
    #if MACOS
    return mac_only_call()
    #else
    return 42
    #end
}
#end

fn main() {
    string p = platform_name()
    if strlen(p) <= 0 {
        print("FAIL: platform_name returned empty")
        return
    }
    print("PASS: conditional fn definition (active branch)")
    print(p)

    int r = outer_win()
    if r != 42 {
        print("FAIL: nested conditional returned ")
        print(r)
        return
    }
    print("PASS: nested #if (inner #else taken)")

    print("ALL PASS")
}
