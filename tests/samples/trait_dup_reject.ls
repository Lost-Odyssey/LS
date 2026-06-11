// trait_dup_reject.ls — should produce compile error: duplicate trait name

trait Printable {
    fn to_string(&self) -> Str
}

trait Printable {
    fn display(&self) -> Str
}

fn main() {
    print(42)
}
