/* Step 4: &!x with same variable later (as &x in 3-arg call). */
fn f(&!string a, int n, &string c) {
    print(a)
    print(n)
    print(c)
}

fn main() -> int {
    string x = "hi".upper()
    f(&!x, 42, x)
    return 0
}
