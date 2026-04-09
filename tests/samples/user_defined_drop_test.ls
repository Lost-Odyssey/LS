// Test: middle struct has user-defined __drop() that properly calls member __drop()
struct Inner {
    string data;
}

struct Outer {
    Inner inner;
}

impl Outer {
    fn __drop() {
        print("Outer.__drop called")
        self.inner.__drop()
    }
}

fn main() {
    Outer o
    o.inner.data = "test".upper()
    print(o.inner.data)
    print("main: exiting...")
    // o goes out of scope - should call Outer.__drop() -> Inner.__drop()
}