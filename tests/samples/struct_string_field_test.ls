// Test: struct with string field - does the string get properly freed?
struct Person {
    string name;
    int age;
}

fn main() {
    Person p
    p.name = "Alice"
    p.age = 30
    print("Person: ", p.name, ", age: ", p.age)
    
    // p goes out of scope here - does p.name get freed?
}
