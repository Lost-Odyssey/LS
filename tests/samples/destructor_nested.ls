module main

struct Inner {
    int id
}

impl Inner {
    fn __drop() {
        print("    [drop] Inner:", self.id)
    }

    static fn create(int val) -> Inner {
        Inner i
        i.id = val
        return i
    }
}

struct Outer {
    Inner inner
    int value
}

impl Outer {
    fn __drop() {
        print("  [drop] Outer:", self.value)
    }
}

struct Deep {
    Outer outer
    string tag
}

impl Deep {
    fn __drop() {
        print("[drop] Deep:", self.tag)
    }
}

fn test_two_level_nesting() {
    print("=== Test: Two-level nesting ===")
    Outer o
    o.inner = Inner.create(1)
    o.value = 100
    print("  Outer created, leaving function")
}

fn test_three_level_nesting() {
    print("=== Test: Three-level nesting ===")
    Deep d
    d.outer.inner = Inner.create(2)
    d.outer.value = 200
    d.tag = "deep_nested"
    print("  Deep created, leaving function")
}

fn test_multiple_nested() {
    print("=== Test: Multiple nested structs ===")
    Outer o1
    o1.inner = Inner.create(10)
    o1.value = 101
    Outer o2
    o2.inner = Inner.create(20)
    o2.value = 202
    print("  Both Outers created, leaving function")
}

fn test_nested_in_loop() {
    print("=== Test: Nested in loop ===")
    for i in 3 {
        Outer o
        o.inner = Inner.create(100 + i)
        o.value = 1000 + i * 10
        print("  iteration", i)
    }
    print("  Loop ended")
}

fn test_nested_in_condition(bool flag) {
    print("=== Test: Nested in condition ===")
    Outer o
    o.inner = Inner.create(300)
    o.value = 301

    if (flag) {
        Deep d
        d.outer.inner = Inner.create(302)
        d.outer.value = 303
        d.tag = "conditional"
        print("  Inside if block")
    }
    print("  Leaving function")
}

fn test_nested_return() -> Outer {
    print("=== Test: Nested struct return ===")
    Outer o
    o.inner = Inner.create(400)
    o.value = 401
    print("  Returning Outer, should skip Inner.__drop but call Outer.__drop")
    return o
}

fn test_nested_swap() {
    print("=== Test: Nested struct swap ===")
    Outer o1
    o1.inner = Inner.create(500)
    o1.value = 501
    Outer o2
    o2.inner = Inner.create(502)
    o2.value = 503
    print("  Both created, values will be swapped")
}

fn main() -> int {
    print("==========================================")
    print("   Nested Struct Destructor E2E Test")
    print("==========================================")
    print("")

    test_two_level_nesting()
    print("")

    test_three_level_nesting()
    print("")

    test_multiple_nested()
    print("")

    test_nested_in_loop()
    print("")

    test_nested_in_condition(true)
    print("")

    test_nested_in_condition(false)
    print("")

    Outer ret = test_nested_return()
    print("  Received returned Outer, leaving main")
    print("")

    test_nested_swap()
    print("")

    print("==========================================")
    print("   All nested destructor tests completed")
    print("==========================================")
    print("")
    print("Expected order: LIFO (reverse of creation)")
    print("  - Inner.__drop before Outer.__drop (member cleanup)")
    print("  - Functions cleanup in reverse call order")
    return 0
}
