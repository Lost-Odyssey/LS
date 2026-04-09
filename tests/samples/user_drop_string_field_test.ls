// Test: struct with string field + user-defined __drop()
struct Person {
    string name;
    int age;
}

impl Person {
    fn __drop() {
        print("Person.__drop called")
    }
}

fn main() {
    Person p
    p.name = "Alice".upper()
    p.age = 30
    print(p.name, p.age)
    print("main: exiting...")
}
