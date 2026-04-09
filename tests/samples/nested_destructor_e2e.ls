// End-to-End Test for Nested Struct Destructor Fix
// This test verifies that nested struct destructors are called correctly

struct Inner {
    string name;
    int value;
}

impl Inner {
    fn __drop() {
        print("Inner.__drop called:", self.value)
    }
}

struct Outer {
    Inner inner;
    string description;
    int id;
}

impl Outer {
    fn __drop() {
        print("Outer.__drop called:", self.id)
    }
}

fn test_nested_destructors() {
    print("=== Testing Nested Struct Destructors ===")
    
    // Test 1: Simple nested struct
    Outer obj1
    obj1.id = 1
    obj1.description = "test1"
    obj1.inner.name = "alpha"
    obj1.inner.value = 100
    print("  Created obj1")
    
    // Test 2: Multiple nested structs
    Outer obj2
    obj2.id = 2
    obj2.description = "test2"
    obj2.inner.name = "beta"
    obj2.inner.value = 200
    print("  Created obj2")
    
    // Test 3: Nested in conditional block
    if (true) {
        Outer obj3
        obj3.id = 3
        obj3.description = "test3"
        obj3.inner.name = "gamma"
        obj3.inner.value = 300
        print("  Created obj3 in conditional block")
    }
    
    // Test 4: Nested in loop
    for i in 2 {
        Outer obj
        obj.id = 10 + i
        obj.description = "loop_test"
        obj.inner.name = "loop_"  // Simplified for testing
        obj.inner.value = 400 + i
        print("  Created obj in loop iteration", i)
    }
    
    print("  Exiting test_nested_destructors")
}

fn test_deep_nesting() {
    print("=== Testing Deep Nesting ===")
    
    struct Level3 {
        string name;
        int value;
    }
    
    struct Level2 {
        string name;
        Level3 level3;
    }
    
    struct Level1 {
        string name;
        Level2 level2;
    }
    
    impl Level3 {
        fn __drop() {
            print("  Level3.__drop called")
        }
    }
    
    impl Level2 {
        fn __drop() {
            print("  Level2.__drop called")
        }
    }
    
    impl Level1 {
        fn __drop() {
            print("  Level1.__drop called")
        }
    }
    
    Level1 l1
    l1.name = "top_level"
    l1.level2.name = "middle_level"
    l1.level2.level3.name = "deep_level"
    l1.level2.level3.value = 999
    
    print("  Created deeply nested struct")
    print("  Exiting test_deep_nesting")
}

fn string_from_int(int n) -> string {
    // Simplified implementation for testing
    if (n == 0) return "0"
    if (n == 1) return "1"
    return "2"  // Simplified for testing
}

fn main() -> int {
    print("=== Nested Struct Destructor E2E Test ===")
    print("")
    
    test_nested_destructors()
    print("")
    
    test_deep_nesting()
    print("")
    
    print("=== Test completed ===")
    print("Expected order:")
    print("- Inner destructors before Outer destructors")
    print("- Deep nesting: Level3 -> Level2 -> Level1")
    print("- Proper reverse order cleanup")
    
    return 0
}