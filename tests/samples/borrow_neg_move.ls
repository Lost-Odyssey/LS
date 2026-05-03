/* Phase 5 negative: cannot move a borrowed string. */

vec(string) g_bucket

fn stash(&string s) {
    g_bucket.push(s)   /* ERROR: cannot move borrowed variable 's' */
}

fn main() -> int {
    string name = "Alice".upper()
    stash(name)
    return 0
}
