// trait_dup_reject.ls — should produce compile error: duplicate trait name

trait Printable {
    fn to_string(&self) -> string
}

trait Printable {
    fn display(&self) -> string
}

fn main() {
    print(42)
}
