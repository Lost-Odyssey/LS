// vec_struct_clone_test.ls — Vec(struct-with-Str) element clone independence.
// VR-LIM-008 (fixed F3): print(vp[0]) — an owned has_drop struct rvalue from an
// index clone — is now dropped after printing (no leak).

import std.vec

struct Person {
    int age;
    int id;
    Str name;
}

fn main() -> int {
    Vec(Person) vp = {}
    Person p1 = Person{age:10, id:1, name:"Kimy".upper()}
    print(p1)

    vp.push(p1)
    Person p2 = vp[0]

    print(p2)
    print(vp[0])                         // F3: index-clone rvalue dropped after print

    // Modify p2 to verify independence
    p2.name = "Alice"
    print(p2)
    print(vp[0])                         // F3: idem
    return 0
}
