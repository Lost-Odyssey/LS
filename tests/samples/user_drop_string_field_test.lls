// Test: struct with Str field + user-defined __drop()
struct Person {
    Str name;
    int age;
}

methods Person {
}

methods Person: Destroy {
    def ~(&!self) {
        @print("Person.__drop called")
    }
}

def main() {
    Person p
    p.name = "Alice".upper()
    p.age = 30
    @print(p.name, p.age)
    @print("main: exiting...")
}
