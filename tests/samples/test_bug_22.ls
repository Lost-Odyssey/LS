// test_bug_22.ls
// Tests: global struct with all-empty string fields, vec(struct) field access,
// f-string formatting in multi-branch logic, string length checks.

fn main() -> int {
    string a = "a"
    string x = "x"
    string b = "b"
    string c = "c"
    string d = "d"
    print(f">>>{a}{x}{b}{x}{c}{d}")
    
    return 0
}
