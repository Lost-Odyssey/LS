// Test: Outer has NO user-defined __drop, should auto-generate that calls Inner
struct Inner {
    Str data;
}

struct Outer {
    Inner inner;
}
// No impl for Outer - compiler should auto-generate __drop that cleans up Inner

def main() {
    Outer o
    o.inner.data = "test".upper()
    @print(o.inner.data)
    @print("main: exiting...")
    // o goes out of scope - compiler should auto-call Inner's cleanup
}