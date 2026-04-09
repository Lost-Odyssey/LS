// vec_struct_test.ls — vec containing structs with drop semantics

int drop_count = 0

struct Item {
    string name;
    int value;
}

impl Item {
    fn __drop() {
        drop_count = drop_count + 1
    }

    static fn make(string n, int v) -> Item {
        Item it
        it.name = n
        it.value = v
        return it
    }
}

fn make_items() -> int {
    vec(Item) v
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

    if (v.length != 3) { return -1 }

    // Sum values via for-in
    int total = 0
    for it in v { total = total + it.value }
    if (total != 60) { return -2 }

    return 1
    // scope exit: drop called for each of 3 elements
}

fn pop_test() -> int {
    int before = drop_count
    vec(Item) v
    Item x
    x.name = "x"
    x.value = 1
    Item y
    y.name = "y"
    y.value = 2
    v.push(x)
    v.push(y)
    v.pop()   // drops y
    int after_pop = drop_count
    // one drop should have happened (y)
    if (after_pop - before != 1) { return -1 }
    return 1
    // scope exit: drops x (one more)
}

fn clear_test() -> int {
    int before = drop_count
    vec(Item) v
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
    v.clear()     // drops all 3
    int after = drop_count
    if (after - before != 3) { return -1 }
    if (v.length != 0) { return -2 }
    return 1
    // scope exit: vec is empty, no further drops
}

fn main() -> int {
    int r1 = make_items()
    print(r1)          // 1
    print(drop_count)  // 3  (a, b, c dropped on scope exit)

    int r2 = pop_test()
    print(r2)          // 1

    int r3 = clear_test()
    print(r3)          // 1

    return 0
}
