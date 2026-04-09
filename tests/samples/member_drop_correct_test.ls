struct Inner {
    *u8 data;
}

impl Inner {
    fn __drop() {
        print("Inner.__drop called")
    }
}

struct Outer {
    Inner a;
    Inner b;
}

impl Outer {
    fn __drop() {
        print("Outer.__drop called")
        self.a.__drop()
        self.b.__drop()
    }
}

fn main() {
    print("test")
}
