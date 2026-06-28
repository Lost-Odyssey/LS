// selftest_pass.ls — fixture for `ls test`: all tests pass (exit 0).
import std.core.test as t
import std.core.vec

def test_arith() {
    t.expect(2 + 3 == 5)
    t.expect_eq_int(6 * 7, 42)
}

def test_vec() {
    Vec(int) v = {}
    v.push(10)
    v.push(20)
    t.expect_eq_int(v.len(), 2)
    t.expect_eq_int(v.get!(1), 20)
}

def test_str_and_float() {
    Str s = "ok"
    t.expect_eq_str(s, "ok")
    t.expect_near(3.14, 3.14001, 0.001)
}

def helper_ignored() { @print("not a test") }
