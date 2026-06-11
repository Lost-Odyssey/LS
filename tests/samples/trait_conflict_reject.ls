// trait_conflict_reject.ls — negative: conflicting method names across traits
// should produce compile error: "conflicting method 'greet'"

trait Greet {
    fn greet(&self) -> Str
}

trait HasValue {
    fn greet(&self) -> int
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
    fn greet(&self) -> int {
        return self.age
    }
}

fn main() -> int {
    Person p = Person { name: "Alice", age: 30 }
    return 0
}