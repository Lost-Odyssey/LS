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

fn test_multiple_persons() {
    print("=== Testing Multiple Persons ===")
    
    Person p1
    p1.name = "Bob"
    p1.age = 25
    p1.address.street = "456 Oak St"
    p1.address.city = "Boston"
    
    Person p2
    p2.name = "Charlie"
    p2.age = 35
    p2.address.street = "789 Pine St"
    p2.address.city = "Seattle"
    
    print("  Created multiple Persons")
    
    // This should call:
    // 1. Person.__drop (Charlie)
    // 2. Address.__drop (789 Pine St)
    // 3. Person.__drop (Bob)
    // 4. Address.__drop (456 Oak St)
}

fn main() -> int {
    print("=== Complex Nested Struct Destructor E2E Test ===")
    print("")
    
    test_person_address()
    print("")
    
    test_multiple_persons()
    print("")
    
    print("=== Test completed ===")
    print("Expected order:")
    print("- Inner destructors before outer destructors")
    print("- Proper reverse order cleanup")
    
    return 0
}