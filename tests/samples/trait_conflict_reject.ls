// trait_conflict_reject.ls — negative: SAME-ORIGIN duplicate method must error.
// L-002 allows the SAME method name from DIFFERENT origins (two interfaces, or
// inherent + interface) to coexist — see iface_method_disambig_test.ls. What is
// still rejected is a true same-origin duplicate: two inherent `greet` methods.
// Expected compile error: "conflicting method 'greet'".

struct Person {
    Str name
    int age
}

methods Person {
    def greet(&self) -> Str {
        return self.name
    }
    def greet(&self) -> Str {
        return self.name
    }
}

def main() -> int {
    Person p = Person { name: "Alice", age: 30 }
    return 0
}
