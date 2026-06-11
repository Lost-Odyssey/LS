import std.vec
import std.str

fn check_int(int got, int want, Str name) {
    if got == want { return }
    print(f"FORCEUNWRAP FAIL: {name}")
}

// Build a genuinely owned (cap>0) Str so move semantics are exercised
// (Str literals are static/cap==0 and would mask a double-free).
fn mk_owned_str(Str tail) -> Option(Str) {
    Str base = "owned-"
    return Some(base + tail)
}

fn mk_owned_vec(int n) -> Option(Vec(int)) {
    Vec(int) v = []
    v.push(n)
    v.push(n + 1)
    return Some(v)
}

fn get_some(int x) -> Option(int) {
    if x > 0 { return Some(x) }
    return None
}

fn get_ok(int x) -> Result(int, Str) {
    if x > 0 { return Ok(x) }
    return Err("bad")
}

struct Box { int n }

impl Box {
    fn maybe(&self) -> Option(int) {
        if self.n > 0 { return Some(self.n) }
        return None
    }

    fn result(&self) -> Result(int, Str) {
        if self.n > 0 { return Ok(self.n) }
        return Err("bad")
    }
}

fn main() {
    // Test 1: Option Some force-unwrap on variable.
    // NOTE: a bare `a!` lexes as a no-arg bang-method name (`!`/`?` are greedy
    // identifier suffixes); to force-unwrap a bare variable, parenthesize: `(a)!`.
    Option(int) a = Some(42)
    check_int((a)!, 42, "option.some")

    // Test 2: Option Some from function
    Option(int) b = get_some(10)
    check_int((b)!, 10, "option.some.fn")

    // Test 3: Force-unwrap on method returning Option
    Box box = Box { n: 7 }
    check_int(box.maybe()!, 7, "method.option")

    // Test 4: Force-unwrap on method returning Result
    check_int(box.result()!, 7, "method.result")

    // Test 5: Force-unwrap on Result Ok
    Result(int, Str) r1 = Ok(55)
    check_int((r1)!, 55, "result.ok")

    // Test 6: Force-unwrap on Result from function
    Result(int, Str) r2 = get_ok(33)
    check_int((r2)!, 33, "result.ok.fn")

    // Test 7: Nested usage - force-unwrap in expression
    check_int(get_some(5)! + 10, 15, "nested.expr")

    // Test 8: owned string success type, variable operand (regression: must
    // move the string out and invalidate the source enum, else double-free).
    Option(Str) os = mk_owned_str("abcdef")   // owned, cap>0
    Str su = (os)!
    check_int(su.len(), 12, "owned.string.var")   // "owned-abcdef" == 12

    // Test 9: owned Str success type, rvalue operand
    Str sr = mk_owned_str("xy")!
    check_int(sr.len(), 8, "owned.string.rvalue") // "owned-xy" == 8

    // Test 10: owned Vec success type — variable + rvalue operands
    Option(Vec(int)) ov = mk_owned_vec(40)
    Vec(int) wv = (ov)!
    check_int(wv.len(), 2, "owned.vec.var")
    check_int(wv.get(0), 40, "owned.vec.elem")
    Vec(int) rv = mk_owned_vec(7)!
    check_int(rv.len(), 2, "owned.vec.rvalue")

    print("FORCEUNWRAP PASS")
}
