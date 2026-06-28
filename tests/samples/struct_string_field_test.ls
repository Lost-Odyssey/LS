// Test: struct with Str field - does the Str get properly freed?
import std.core.str

struct Person {
    Str name
    int age
}

def main() {
    Person p
    p.name = "Alice"
    p.age = 30
    @print("Person: ", p.name, ", age: ", p.age)

    // p goes out of scope here - does p.name get freed?
}
