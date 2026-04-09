// Test: struct with string field - implicit copy should be forbidden
struct Person {
    string name;
    int age;
}

fn main() {
    Person p1
    p1.name = "Alice"
    p1.age = 30
    
    // This should produce an error: cannot implicitly copy struct with string fields
    Person p2 = p1
    
    print(p2.name)
}