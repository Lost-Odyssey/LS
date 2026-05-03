struct Person {
    int age;
    string name;
}

fn main() -> int {
    Person p1 = Person{age:10, name:"Alice".upper()}
    Person p2 = Person{age:20, name:"Bob".upper()}

    // p1 = p2: should drop p1's old data, deep-clone p2 into p1
    p1 = p2
    p2.name = "Charlie"   // modify p2 — must not affect p1

    print(p1)   // expect: Person{age=20, name=BOB}
    print(p2)   // expect: Person{age=20, name=Charlie}

    return 0
}
