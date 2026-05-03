/* Step 7: mut borrow used inside a loop — mutations accumulate in caller. */
fn add_n(&!string s, int n) {
    for (int i = 0; i < n; i = i + 1) {
        s.append('.')
    }
}

fn main() -> int {
    string x = "go".upper()
    add_n(&!x, 3)
    print(x)  /* expect: GO... */
    return 0
}
