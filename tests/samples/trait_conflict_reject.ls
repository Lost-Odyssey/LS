// trait_conflict_reject.ls — negative: conflicting method names across traits
// should produce compile error: "conflicting method 'greet'"

trait Greet {
    fn greet(&self) -> string
}

trait HasValue {
    fn greet(&self) -> int
}

struct Person {
    string name
    int age
}

impl Greet for Person {
    fn greet(&self) -> string {
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