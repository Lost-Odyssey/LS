module mod_a

struct Widget { string name; int v }
impl Widget {
    fn val(&self) -> int { return self.v * 10 }
    static fn make(int n) -> Widget { return Widget { name: "a".upper(), v: n } }
}

enum Kind { Small, Big }
impl Kind {
    fn rank(&self) -> int { match self { Small => { return 1 } Big => { return 2 } } }
}
fn big() -> Kind { return Big }
