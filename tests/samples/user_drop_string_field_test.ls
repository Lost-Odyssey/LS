// Test: struct with Str field + user-defined __drop()
struct Person {
    Str name;
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
