// Test: temporary string expression cleanup in print
fn main() {
    print("hello".upper())          // Should not leak
    print("abc".trim().upper())     // Should not leak
    print("test" + " " + "world")   // Should not leak
    
    // Regular variable case should still work
    string s = "hello".upper()
    print(s)
    // s is properly freed at scope exit
}