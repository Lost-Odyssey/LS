struct Inner {
    *u8 data;
}

impl Inner {
    fn __drop() {
        print("Inner.__drop called")
    }
}

struct Outer {
    Inner child;
}

impl Outer {
    fn __drop() {
        print("Outer.__drop called")
        self.child.__drop()
    }
}

fn main() {
    print("Creating Outer with Inner child...")
    Outer o
    
    print("About to exit main scope...")
}
