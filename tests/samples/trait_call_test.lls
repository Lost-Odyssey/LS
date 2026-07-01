// trait_call_test.ls — Step 6: verify trait methods can be called

interface Greet {
    def greet(&self) -> Str
}

interface HasValue {
    def value(&self) -> int
}

struct Person {
    Str name
    int age
}

methods Person: Greet {
    def greet(&self) -> Str {
        return self.name
    }
}

methods Person: HasValue {
    def value(&self) -> int {
        return self.age
    }
}

// Regular impl still works alongside trait impls
methods Person {
    static def create(Str n, int a) -> Person {
        return Person { name: n, age: a }
    }
}

def main() {
    Person p = Person.create("Alice", 30)
    @print(p.greet())    // Alice
    @print(p.value())    // 30

    // Direct struct field access still works
    @print(p.age)        // 30

    // Trait method with &!self
    @print(99)           // sentinel
}
