// Phase E.3.1 — errno() builtin
// Triggers a libc failure (fopen on a non-existent file) and verifies that
// errno() returns a non-zero value. Pairs with strerror via from_cstr to
// produce a human-readable error string in pure LS.

extern fn fopen(string path, string mode) -> object
extern fn strerror(int e) -> object

fn main() {
    // Sanity: errno() compiles + runs and returns an int.
    int e0 = errno()
    print("PASS: errno() callable (current value follows)")
    print(e0)

    // Trigger ENOENT by opening a path that does not exist.
    object handle = fopen("definitely_does_not_exist_xyz_42.txt", "rb")
    int e = errno()
    if e == 0 {
        print("FAIL: expected non-zero errno after failing fopen")
        return
    }
    print("PASS: fopen of missing file produces errno != 0")
    print(e)

    // Wrap strerror via from_cstr to get a managed LS string.
    object msg_p = strerror(e)
    string msg = from_cstr(msg_p)
    if msg.empty() {
        print("FAIL: strerror returned empty string")
        return
    }
    print("PASS: from_cstr(strerror(errno)) yields LS string:")
    print(msg)

    print("ALL PASS")
}
