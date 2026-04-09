// Test: struct defined BEFORE nested struct with string
// Forward reference - Outer defined before Inner
struct Outer {
    Inner inner;  // Inner not yet defined here!
}

struct Inner {
    string data;
}

fn main() {
    Outer o
    o.inner.data = "test".upper()
    print(o.inner.data)
}