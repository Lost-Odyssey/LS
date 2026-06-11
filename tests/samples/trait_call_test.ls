// trait_call_test.ls — Step 6: verify trait methods can be called

trait Greet {
    fn greet(&self) -> Str
}

trait HasValue {
    fn value(&self) -> int
}

struct Person {
    Str name
    int age
}

impl Greet for Person {
    fn greet(&self) -> Str {
        return self.name
    }
}

impl HasValue for Person {
    fn value(&self) -> int {
        return self.age
    }
}

// Regular impl still works alongside trait impls
impl Person {
    static fn create(Str n, int a) -> Person {
        return Person { name: n, age: a }
    }
}

fn main() {
    Person p = Person.create("Alice", 30)
    print(p.greet())    // Alice
    print(p.value())    // 30

    // Direct struct field access still works
    print(p.age)        // 30

    // Trait method with &!self
    print(99)           // sentinel
}
