// trait_int_impl_test.ls — Step 11: impl trait for builtin types

interface Describable {
    def describe(&self) -> Str
}

methods int: Describable {
    def describe(&self) -> Str {
        return f"int:{self}"
    }
}

methods f64: Describable {
    def describe(&self) -> Str {
        return f"f64:{self}"
    }
}

methods bool: Describable {
    def describe(&self) -> Str {
        if self {
            return "bool:true"
        }
        return "bool:false"
    }
}

// Constrained generic using Describable
def show(T: Describable)(T x) {
    @print(x.describe())
}

def main() {
    // Direct method calls
    int x = 42
    @print(x.describe())

    f64 y = 3.14
    @print(y.describe())

    bool b = true
    @print(b.describe())

    // Via constrained generic
    show(int)(99)
    show(f64)(2.718)
    show(bool)(false)
}
