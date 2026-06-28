// Phase E.3.1 — errno() builtin
// Triggers a libc failure (fopen on a non-existent file) and verifies that
// errno() returns a non-zero value. Pairs with strerror via from_cstr to
// produce a human-readable error string.
// FFI red line: extern fns take *u8; Str crosses the boundary via c_str().
import std.core.str

extern def fopen(*u8 path, *u8 mode) -> object
extern def strerror(int e) -> object

def main() {
    // Sanity: errno() compiles + runs and returns an int.
    int e0 = errno()
    @print("PASS: errno() callable (current value follows)")
    @print(e0)

    // Trigger ENOENT by opening a path that does not exist.
    Str path = "definitely_does_not_exist_xyz_42.txt"
    Str mode = "rb"
    object handle = fopen(path.c_str(), mode.c_str())
    int e = errno()
    if e == 0 {
        @print("FAIL: expected non-zero errno after failing fopen")
        return
    }
    @print("PASS: fopen of missing file produces errno != 0")
    @print(e)

    // Wrap strerror via from_cstr to get a managed Str.
    object msg_p = strerror(e)
    Str msg = from_cstr(msg_p)
    if msg.empty?() {
        @print("FAIL: strerror returned empty string")
        return
    }
    @print("PASS: from_cstr(strerror(errno)) yields Str:")
    @print(msg)

    @print("ALL PASS")
}
