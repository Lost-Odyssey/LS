struct Person {
    int age;
    int id;
    string name;
}

fn main() -> int {
    vec(Person) vp
    Person p1 = Person{age:10, id:1, name:"Kimy".upper()}
    print(p1)

    vp.push(p1)
    Person p2 = vp[0]

    print(p2)
    print(vp[0])

    // Modify p2 to verify independence
    p2.name = "Alice"
    print(p2)
    print(vp[0])
    return 0
}
