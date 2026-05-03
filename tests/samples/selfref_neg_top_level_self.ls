/* Neg: top-level fn cannot use &!self / &self. */
fn foo(&!self) {
    print(1)
}

fn main() -> int {
    return 0
}
