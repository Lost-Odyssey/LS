// Test: struct with Str field - variable assignment moves the struct
import std.core.str

struct Person {
    Str name
    int age
}

def main() {
    Person p1
    p1.name = "Alice"
    p1.age = 30

    // `Person p2 = p1` moves p1 into p2 (has_drop struct move); p1 is dead after.
    Person p2 = p1

    @print(p2.name)
}
