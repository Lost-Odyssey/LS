/* struct_string_e2e.ls — E2E tests for struct with string fields
*/

struct Person {
    string name
    int age
}

fn print_person(Person p) {
    print("Name: " + p.name)
}

fn test_struct_with_string() {
    // Test 1: Assign dynamic string to struct field
    Person p1
    p1.name = "Alice".upper()
    p1.age = 30
    print_person(p1)
    
    // Test 2: Create new struct, don't reuse moved variable
    Person p2
    p2.name = "Bob".lower()
    p2.age = 25
    print_person(p2)
    
    // Test 3: Assign static string
    Person p3
    p3.name = "Charlie"
    p3.age = 35
    print_person(p3)
    
    // Test 4: String variable move to struct field
    string s = "David".upper()
    Person p4
    p4.name = s
    p4.age = 40
    print_person(p4)
    // s is moved here
    
    // Test 5: Static string copy (no move)
    string t = "Eve"
    Person p5
    p5.name = t
    p5.age = 45
    print_person(p5)
}

fn main() -> int {
    test_struct_with_string()
    print("Struct with string fields test passed!")
    return 0
}
