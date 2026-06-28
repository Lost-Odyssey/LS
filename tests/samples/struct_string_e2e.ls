/* struct_string_e2e.ls — E2E tests for struct with Str fields
*/
import std.core.str

struct Person {
    Str name
    int age
}

def print_person(Person p) {
    @print(f"Name: {p.name}")
}

def test_struct_with_string() {
    // Test 1: Assign dynamic Str to struct field
    Person p1
    Str a = "Alice"
    p1.name = a.upper()
    p1.age = 30
    print_person(p1)

    // Test 2: Create new struct, don't reuse moved variable
    Person p2
    Str b = "Bob"
    p2.name = b.lower()
    p2.age = 25
    print_person(p2)

    // Test 3: Assign static Str
    Person p3
    p3.name = "Charlie"
    p3.age = 35
    print_person(p3)

    // Test 4: Str variable move to struct field
    Str dv = "David"
    Str s = dv.upper()
    Person p4
    p4.name = s
    p4.age = 40
    print_person(p4)
    // s is moved here

    // Test 5: Static Str copy (no heap)
    Str t = "Eve"
    Person p5
    p5.name = t
    p5.age = 45
    print_person(p5)
}

def main() -> int {
    test_struct_with_string()
    @print("Struct with string fields test passed!")
    return 0
}
