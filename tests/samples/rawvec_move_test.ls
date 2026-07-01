import std.core.str

struct RawVecS { *Str data; int len; int cap }
def new_rvs() -> RawVecS { *Str p = nil; return RawVecS { data: p, len: 0, cap: 0 } }
methods RawVecS {
    def push(&!self, Str x) {
        if self.len >= self.cap {
            int n = 4
            if self.cap > 0 { n = self.cap * 2 }
            self.data = std.sys.c.realloc(self.data as *u8, n * sizeof(Str)) as *Str
            self.cap = n
        }
        self.data[self.len] = x
        self.len = self.len + 1
    }
    def get(&self, int i) -> Str { Str t = self.data[i]; return t }
}

methods RawVecS: Destroy {
    def ~(&!self) {
        for (int i = 0; i < self.len; i = i + 1) { @dispose(self.data[i]) }
        if self.cap > 0 { std.sys.c.free(self.data as *u8) }
    }
}

def check(bool c, Str l) { if c { @print(f"ok {l}") } else { @print(f"FAIL {l}") } }

def main() {
    RawVecS v = new_rvs()
    for i in 0..3 { v.push(f"item_{i}") }
    check(v.len == 3, "rvalue push len")
    check(v.get(0).eq?("item_0"), "rvalue push value")

    Str x = "hello world"
    v.push(x)
    check(v.get(v.len-1).eq?("hello world"), "named var push value")
    check(x.eq?("hello world"), "named var still valid")

    Str y = f"move_test"
    v.push(@move(y))
    check(v.get(v.len-1).eq?("move_test"), "@move push value")

    RawVecS v2 = new_rvs()
    for i in 0..10 { v2.push(f"n{i}") }
    check(v2.len == 10, "multiple rvalue push len")
    check(v2.get(5).eq?("n5"), "multiple rvalue push value")

    @print("ALL PASS")
}
