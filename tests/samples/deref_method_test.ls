// Regression for the codegen_addr_of deref fix: calling a &!self / &self method
// through a RAW-POINTER deref receiver `(*ptr).method()` must operate on the
// pointee, not a spilled rvalue COPY. Before the fix, addr_of had no AST_UNARY(*)
// case, so the method-call path spilled a copy and mutations were lost (the
// pointed-to struct stayed untouched). This is what lets ChanIter (holding a raw
// *Chan) drive recv() for `for x in ch`.

struct Counter { int n }
methods Counter {
    def bump(&!self) -> int { self.n = self.n + 1  return self.n }
    def val(&self) -> int { return self.n }
}

struct Ref { *Counter c }
methods Ref {
    def bump(&!self) -> int { return (*self.c).bump() }   // mutate through pointer
    def read(&self) -> int { return (*self.c).val() }     // read through pointer
}

def main() -> int {
    Counter c = Counter { n: 0 }
    Ref r = Ref { c: &c }       // &c yields a raw *Counter to c

    int a = r.bump()            // 1
    int b = r.bump()            // 2
    int d = r.read()            // 2

    if a == 1 && b == 2 && d == 2 && c.n == 2 {
        @print("DEREF OK")
    } else {
        @print("DEREF FAIL")
        @print(a)
        @print(b)
        @print(d)
        @print(c.n)
    }
    return 0
}
