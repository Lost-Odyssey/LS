/* string_memory_e2e.ls — E2E tests for string memory management
   Tests temporary string cleanup, static vs dynamic strings, etc.
*/

// Test 1: Static string should work normally (no free needed)
fn test_static_string() {
    string s = "hello"
    string t = s
    print("Test 1 - static string:", s, t)
}

// Test 2: Dynamic string should be freed on scope exit
fn test_dynamic_string_cleanup() {
    if (true) {
        string s = "hello".upper()
        print("Test 2 - dynamic string:", s)
    }
    print("Test 2 - after block exit")
}

// Test 3: Return static string
fn test_return_static() -> string {
    string s = "world"
    return s
}

// Test 4: Multiple string operations
fn test_multiple_strings() {
    string a = "hello".upper()
    string b = "WORLD".lower()
    string c = a + " " + b
    print("Test 4 - multiple strings:", c)
}

// Test 5: Chain method calls
fn test_chain_methods() {
    string result = "  hello world  ".trim().upper()
    print("Test 5 - chain methods:", result)
}

// Test 6: String in loop
fn test_string_in_loop() {
    for i in 3 {
        string s = "iteration".upper()
        print("Test 6 - in loop:", i, s)
    }
    print("Test 6 - after loop")
}

// Test 7: String concat as expression
fn test_concat_expression() {
    string a = "hello"
    string b = "world"
    string c = a + " " + b
    print("Test 7 - concat:", c)
}

// Test 8: Format string
fn test_format_string() {
    int x = 42
    string s = f"answer = {x}"
    print("Test 8 - format:", s)
}

// Test 9: Method result as statement (temporary should be cleaned)
fn test_method_as_statement() {
    string s = "test"
    s.upper()  // temporary result should be cleaned
    s.lower()  // another temporary
    print("Test 9 - after temp cleanup:", s)
}

// Test 10: Reassignment frees old value
fn test_reassignment() {
    string s = "initial"
    s = "modified".upper()
    print("Test 10 - reassigned:", s)
}

fn main() -> int {
    test_static_string()
    test_dynamic_string_cleanup()
    
    string r1 = test_return_static()
    print("Test 3 - returned:", r1)
    
    test_multiple_strings()
    test_chain_methods()
    test_string_in_loop()
    test_concat_expression()
    test_format_string()
    test_method_as_statement()
    test_reassignment()
    
    print("All string memory tests passed!")
    return 0
}
