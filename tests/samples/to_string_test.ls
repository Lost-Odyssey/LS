// Test to_string(), from_int(), and from_float() builtin functions
fn main() {
    // Test to_string
    int x = 42
    string s1 = to_string(x)
    print("to_string(int 42): ", s1)

    f64 pi = 3.14159
    string s2 = to_string(pi)
    print("to_string(f64 3.14159): ", s2)

    bool flag = true
    string s3 = to_string(flag)
    print("to_string(bool true): ", s3)

    // Test from_int
    int n1 = from_int("123")
    print("from_int(\"123\"): ", n1)

    int n2 = from_int("-456")
    print("from_int(\"-456\"): ", n2)

    // Test from_float
    f64 f1 = from_float("3.14")
    print("from_float(\"3.14\"): ", f1)

    f64 f2 = from_float("-2.5e-2")
    print("from_float(\"-2.5e-2\"): ", f2)

    // Round-trip test
    int original = 999
    string s = to_string(original)
    int restored = from_int(s)
    print("Round-trip int: ", original, " -> ", s, " -> ", restored)
    if (restored == original) {
        print("Round-trip int test PASSED")
    } else {
        print("Round-trip int test FAILED")
    }

    f64 orig_f = 2.71828
    string sf = to_string(orig_f)
    f64 rest_f = from_float(sf)
    print("Round-trip f64: ", orig_f, " -> ", sf, " -> ", rest_f)

    print("All string conversion tests passed!")
}
