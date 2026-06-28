module mod_b

/* Same names as mod_a (Widget, Kind) but DIFFERENT layouts/methods. B-4.1: their
   impls register under distinct keys (mod_b__Widget vs mod_a__Widget) instead of
   colliding with "conflicting method". */
struct Widget { int code }
methods Widget {
    def val(&self) -> int { return self.code + 100 }
    static def make(int n) -> Widget { return Widget { code: n } }
}

enum Kind { Lo, Hi }
methods Kind {
    def rank(&self) -> int { match self { Lo => { return 30 } Hi => { return 40 } } }
}
def hi() -> Kind { return Hi }
