// vec_struct_test.ls — Vec containing structs with drop semantics
// VR-LIM-007: Vec drop timing differs from built-in vec; use memcheck to verify no leaks.

import std.vec

struct Item {
    string name;
    int value;
}

impl Item {
    fn __drop() { }

    static fn make(string n, int v) -> Item {
        Item it
        it.name = n
        it.value = v
        return it
    }
}

fn make_items() -> int {
    Vec(Item) v = {}
    Item a
    a.name = "alpha"
    a.value = 10
    Item b
    b.name = "beta"
    b.value = 20
    Item c
    c.name = "gamma"
    c.value = 30

    v.push(a)
    v.push(b)
    v.push(c)

    if (v.len() != 3) { return -1 }

    // Sum values via for-in
    int total = 0
    for it in v { total = total + it.value }
    if (total != 60) { return -2 }

    return 1
    // scope exit: Vec.__drop called
}

fn pop_test() -> int {
    Vec(Item) v = {}
    Item x
    x.name = "x"
    x.value = 1
    Item y
    y.name = "y"
    y.value = 2
    v.push(x)
    v.push(y)
    Option(Item) _py = v.pop()     // pop y into Option (dropped at scope exit)
    if (v.len() != 1) { return -1 }
    return 1
    // scope exit: drops remaining x + Option(Item) y
}

fn clear_test() -> int {
    Vec(Item) v = {}
    Item a
    a.name = "a"
    a.value = 1
    Item b
    b.name = "b"
    b.value = 2
    Item c
    c.name = "c"
    c.value = 3
    v.push(a)
    v.push(b)
    v.push(c)
    v.clear()     // Vec.clear calls __drop_at on each element
    if (v.len() != 0) { return -1 }
    return 1
    // scope exit: vec is empty, no further drops
}

fn main() -> int {
    int r1 = make_items()
    print(r1)          // 1

    int r2 = pop_test()
    print(r2)          // 1

    int r3 = clear_test()
    print(r3)          // 1

    return 0
}
