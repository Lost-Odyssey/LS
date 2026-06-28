// End-to-End Test for Nested Struct Destructor Fix
// This test verifies that nested struct destructors are called correctly
import std.core.str

struct Inner {
    Str name;
    int value;
}

methods Inner {
}

methods Inner: Destroy {
    def ~(&!self) {
        @print("Inner.__drop called:", self.value)
    }
}

struct Outer {
    Inner inner;
    Str description;
    int id;
}

methods Outer {
}

methods Outer: Destroy {
    def ~(&!self) {
        @print("Outer.__drop called:", self.id)
    }
}

def test_nested_destructors() {
    @print("=== Testing Nested Struct Destructors ===")
    
    // Test 1: Simple nested struct
    Outer obj1
    obj1.id = 1
    obj1.description = "test1"
    obj1.inner.name = "alpha"
    obj1.inner.value = 100
    @print("  Created obj1")
    
    // Test 2: Multiple nested structs
    Outer obj2
    obj2.id = 2
    obj2.description = "test2"
    obj2.inner.name = "beta"
    obj2.inner.value = 200
    @print("  Created obj2")
    
    // Test 3: Nested in conditional block
    if (true) {
        Outer obj3
        obj3.id = 3
        obj3.description = "test3"
        obj3.inner.name = "gamma"
        obj3.inner.value = 300
        @print("  Created obj3 in conditional block")
    }
    
    // Test 4: Nested in loop
    for i in 2 {
        Outer obj
        obj.id = 10 + i
        obj.description = "loop_test"
        obj.inner.name = "loop"
        obj.inner.value = 400 + i
        @print("  Created obj in loop iteration", i)
    }
    
    @print("  Exiting test_nested_destructors")
}

def main() -> int {
    @print("=== Nested Struct Destructor E2E Test ===")
    @print("")
    
    test_nested_destructors()
    @print("")
    
    @print("=== Test completed ===")
    @print("Expected order:")
    @print("- Inner destructors before Outer destructors")
    @print("- Proper reverse order cleanup")
    
    return 0
}