// test_bug_22.ls
// Tests: global struct with all-empty Str fields, Vec(struct) field access,
// f-Str formatting in multi-branch logic, Str length checks.

fn main() -> int {
    Str a = "a"
    Str x = "x"
    Str b = "b"
    Str c = "c"
    Str d = "d"
    print(f">>>{a}{x}{b}{x}{c}{d}")
    
    return 0
}
