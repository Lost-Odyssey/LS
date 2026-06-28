// trait_conflict_reject.ls — negative: conflicting method names across traits
// should produce compile error: "conflicting method 'greet'"

interface Greet {
    def greet(&self) -> Str
}

interface HasValue {
    def greet(&self) -> int
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
    def greet(&self) -> int {
        return self.age
    }
}

def main() -> int {
    Person p = Person { name: "Alice", age: 30 }
    return 0
}