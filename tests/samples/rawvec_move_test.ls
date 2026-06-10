struct RawVecS { *string data; int len; int cap }
fn new_rvs() -> RawVecS { *string p = nil; return RawVecS { data: p, len: 0, cap: 0 } }
impl RawVecS {
    fn push(&!self, string x) {
        if self.len >= self.cap {
            int n = 4
            if self.cap > 0 { n = self.cap * 2 }
            self.data = std.c.realloc(self.data as *u8, n * sizeof(string)) as *string
            self.cap = n
        }
        self.data[self.len] = x
        self.len = self.len + 1
    }
    fn get(&self, int i) -> string { string t = self.data[i]; return t }
    fn __drop() {
        for (int i = 0; i < self.len; i = i + 1) { __drop_at(self.data[i]) }
        if self.cap > 0 { std.c.free(self.data as *u8) }
    }
}

fn check(bool c, string l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn main() {
    RawVecS v = new_rvs()
    for i in 0..3 { v.push(f"item_{i}") }
    check(v.len == 3, "rvalue push len")
    check(v.get(0) == "item_0", "rvalue push value")

    string x = "hello world"
    v.push(x)
    check(v.get(v.len-1) == "hello world", "named var push value")
    check(x == "hello world", "named var still valid")

    string y = f"move_test"
    v.push(__move(y))
    check(v.get(v.len-1) == "move_test", "__move push value")

    RawVecS v2 = new_rvs()
    for i in 0..10 { v2.push(f"n{i}") }
    check(v2.len == 10, "multiple rvalue push len")
    check(v2.get(5) == "n5", "multiple rvalue push value")

    print("ALL PASS")
}
