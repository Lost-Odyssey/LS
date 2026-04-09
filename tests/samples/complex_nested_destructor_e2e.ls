// Complex E2E Test for Nested Struct Destructor Fix
// This test specifically verifies the bug fix for nested struct destructors

struct Address {
    string street;
    string city;
}

impl Address {
    fn __drop() {
        print("Address.__drop called:", self.street)
    }
}

struct Person {
    string name;
    Address address;
    int age;
}

impl Person {
    fn __drop() {
        print("Person.__drop called:", self.name)
    }
}

struct Company {
    string name;
    Person ceo;
    // Person[] employees;  // This would be complex, let's simplify
}

impl Company {
    fn __drop() {
        print("Company.__drop called:", self.name)
    }
}

fn test_person_address() {
    print("=== Testing Person with Address ===")
    
    Person p
    p.name = "Alice"
    p.age = 30
    p.address.street = "123 Main St"
    p.address.city = "New York"
    
    print("  Created Person with Address")
    
    // This should call:
    // 1. Person.__drop (Alice)
    // 2. Address.__drop (123 Main St)
}

fn test_company_person() {
    print("=== Testing Company with Person ===")
    
    Company c
    c.name = "Tech Corp"
    
    // CEO
    c.ceo.name = "Bob"
    c.ceo.age = 45
    c.ceo.address.street = "Executive Blvd"
    c.ceo.address.city = "Tech City"
    
    print("  Created Company with CEO")
    
    // This should call:
    // 1. Company.__drop (Tech Corp)
    // 2. Person.__drop (Bob)
    // 3. Address.__drop (Executive Blvd)
}

fn test_nested_in_function() -> Person {
    print("=== Testing Nested in Function ===")
    
    fn create_person() -> Person {
        Person p
        p.name = "Charlie"
        p.age = 25
        p.address.street = "Worker Ave"
        p.address.city = "Small Town"
        return p
    }
    
    Person p = create_person()
    print("  Created Person in nested function")
    
    // This should call:
    // 1. Person.__drop (Charlie)
    // 2. Address.__drop (Worker Ave)
    
    return p
}

fn main() -> int {
    print("=== Complex Nested Struct Destructor E2E Test ===")
    print("")
    
    test_person_address()
    print("")
    
    test_company_person()
    print("")
    
    Person p_nested = test_nested_in_function()
    print("")
    
    print("=== Test completed ===")
    print("Expected order:")
    print("- Inner destructors before outer destructors")
    print("- Proper reverse order cleanup")
    
    return 0
}