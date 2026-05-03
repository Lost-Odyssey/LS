/* Phase 5 negative: cannot reassign a borrowed string. */

fn rename(&string s) {
    s = "Bob".upper()   /* ERROR: cannot assign to borrowed variable 's' */
}

fn main() -> int {
    string name = "Alice".upper()
    rename(name)
    return 0
}
