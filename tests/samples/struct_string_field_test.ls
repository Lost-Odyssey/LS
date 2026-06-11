// Test: struct with Str field - does the Str get properly freed?
import std.str

struct Person {
    Str name
    int age
}

fn main() {
    Person p
    p.name = "Alice"
    p.age = 30
    print("Person: ", p.name, ", age: ", p.age)

    // p goes out of scope here - does p.name get freed?
}
