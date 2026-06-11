import std.str

fn ok?(int x) -> bool {
    return x > 0
}

fn danger!(int x) -> int {
    return x * 2
}

fn ready?() -> bool {
    return true
}

fn init!() -> int {
    return 11
}

struct Box { int n }

impl Box {
    fn empty?(&self) -> bool {
        return self.n == 0
    }

    fn touch!(&self, int x) -> int {
        return self.n + x
    }

    fn value!(&self) -> int {
        return self.n
    }
}

fn check_bool(bool got, bool want, Str name) -> bool {
    if got == want { return true }
    print(f"IDENTSUF FAIL: {name}")
    return false
}

fn check_int(int got, int want, Str name) -> bool {
    if got == want { return true }
    print(f"IDENTSUF FAIL: {name}")
    return false
}

fn main() {
    bool ok = true

    ok = check_bool(ok?(3), true, "predicate.true") && ok
    ok = check_bool(ok?(0), false, "predicate.false") && ok
    ok = check_int(danger!(21), 42, "bang.call") && ok
    ok = check_bool(ready?, true, "free.predicate.noargs") && ok
    ok = check_int(init!, 11, "free.bang.noargs") && ok

    Box box = Box { n: 0 }
    ok = check_bool(box.empty?, true, "method.predicate") && ok
    ok = check_int(box.touch!(7), 7, "method.bang") && ok
    ok = check_int(box.value!, 0, "method.bang.noargs") && ok

    int a = 1
    int b = 2
    ok = check_bool(a != b, true, "neq") && ok

    bool flag = false
    ok = check_bool(!flag, true, "prefix.bang") && ok

    if ok { print("IDENTSUF PASS") }
}
