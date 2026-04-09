// Test: f-string assigned to variable - does it get freed?
fn main() {
    int x = 42
    string s = f"value = {x}"
    print(s)
    
    // s goes out of scope here - should be freed
    print("done")
}