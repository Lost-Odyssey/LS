// Test: struct with Str field assigned a dynamic Str
import std.core.str

struct Person {
    Str name
    int age
}

def main() {
    Person p
    p.age = 30

    // Assign a dynamic Str (result of f-string + method)
    Str d = f"{42}"
    p.name = d.upper()

    @print("Person: ", p.name, ", age: ", p.age)

    // p goes out of scope here - p.name is now properly freed via auto-generated __drop
}
