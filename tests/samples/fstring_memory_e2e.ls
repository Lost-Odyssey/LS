// E2E Test: f-string heap allocation and cleanup
// This test verifies that f-strings are properly allocated on the heap
// and cleaned up when they go out of scope.

struct Point {
    int x;
    int y;
}

impl Point {
    fn to_string() -> string {
        return f"Point({self.x}, {self.y})"
    }
}

fn main() {
    // Test 1: f-string as statement (anonymous, should be freed)
    f"Test 1: Anonymous f-string"

    // Test 2: f-string assigned to variable (should be freed at end of block)
    string s1 = f"Test 2: {42}"
    print(s1)

    // Test 3: Multiple f-strings in a loop
    for i in 3 {
        string s = f"Loop iteration {i}"
        print(s)
    }

    // Test 4: Struct to_string method returns heap-allocated string
    Point p
    p.x = 100
    p.y = 200
    string pt = p.to_string()
    print(pt)

    // Test 5: Chained operations on f-string result
    string s2 = f"hello {42}".upper()
    print(s2)

    // Test 6: f-string returned from function
    string result = make_greeting("World")
    print(result)

    // Test 7: Nested f-string with method
    string nested = f"Value: {p.to_string()}"
    print(nested)

    print("All f-string memory management tests passed!")
}

fn make_greeting(string name) -> string {
    return f"Hello, {name}!"
}
