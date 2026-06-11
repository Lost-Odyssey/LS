// trait_struct_bound_test.ls — Step 13: trait bounds on generic struct type params

trait Describable {
    fn describe(&self) -> Str
}

struct Circle {
    f64 radius
}

impl Describable for Circle {
    fn describe(&self) -> Str {
        return f"Circle(r={self.radius})"
    }
}

// Generic struct with trait bound: T must implement Describable
struct Wrapper(T: Describable) {
    T item
}

// Generic function using the constrained struct
fn show_wrapper(T: Describable)(Wrapper(T) w) {
    print(w.item.describe())
}

fn main() {
    Circle c = Circle { radius: 5.0 }
    Wrapper(Circle) w = Wrapper(Circle) { item: c }

    // Direct field access + trait method
    print(w.item.describe())

    // Via generic function
    Circle c2 = Circle { radius: 9.0 }
    Wrapper(Circle) w2 = Wrapper(Circle) { item: c2 }
    show_wrapper(Circle)(w2)
}
