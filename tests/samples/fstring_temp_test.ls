// Test: f-string used as temporary (not assigned to variable)
fn main() {
    int x = 42
    print(f"temp f-string: {x}")
    // Temporary f-string result should be freed after the print call
    
    print("done")
}