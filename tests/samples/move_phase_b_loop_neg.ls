// Expected to fail: loop 2-pass analysis must flag use/move of a variable
// that gets moved inside the body on any iteration.
fn main() -> int {
    vec(string) v
    string s = "world".upper()
    int i = 0
    while i < 3 {
        v.push(s)    // would move s repeatedly -> pass 2 rejects this
        i = i + 1
    }
    return 0
}
