// trait_int_impl_test.ls — Step 11: impl trait for builtin types

trait Describable {
    fn describe(&self) -> string
}

impl Describable for int {
    fn describe(&self) -> string {
        return f"int:{self}"
    }
}

impl Describable for f64 {
    fn describe(&self) -> string {
        return f"f64:{self}"
    }
}

impl Describable for bool {
    fn describe(&self) -> string {
        if self {
            return "bool:true"
        }
        return "bool:false"
    }
}

// Constrained generic using Describable
fn show(T: Describable)(T x) {
    print(x.describe())
}

fn main() {
    // Direct method calls
    int x = 42
    print(x.describe())

    f64 y = 3.14
    print(y.describe())

    bool b = true
    print(b.describe())

    // Via constrained generic
    show(int)(99)
    show(f64)(2.718)
    show(bool)(false)
}
