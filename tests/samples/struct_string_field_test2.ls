// Test: struct with string field assigned a dynamic string
struct Person {
    string name;
    int age;
}

fn main() {
    Person p
    p.age = 30
    
    // Assign a dynamic string (result of to_string)
    p.name = to_string(42).upper()
    
    print("Person: ", p.name, ", age: ", p.age)
    
    // p goes out of scope here - p.name is now properly freed via auto-generated __drop
}
