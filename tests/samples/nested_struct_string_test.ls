// Test: nested struct with Str in deep level
struct Inner {
    Str data;
}

struct Outer {
    Inner inner;
}

def main() {
    Outer o
    o.inner.data = "test".upper()
    @print(o.inner.data)
    // o goes out of scope - should inner.data be freed?
}