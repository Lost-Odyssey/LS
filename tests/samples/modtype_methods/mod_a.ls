module mod_a
import std.core.str

struct Widget { Str name; int v }
methods Widget {
    def val(&self) -> int { return self.v * 10 }
    static def make(int n) -> Widget { return Widget { name: "a".upper(), v: n } }
}

enum Kind { Small, Big }
methods Kind {
    def rank(&self) -> int { match self { Small => { return 1 } Big => { return 2 } } }
}
def big() -> Kind { return Big }
