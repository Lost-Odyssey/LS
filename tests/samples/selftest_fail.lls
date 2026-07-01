// selftest_fail.ls — fixture for `ls test`: one test fails (exit 1).
// Verifies the runner actually FAILS loudly (the anti-"假绿" guarantee).
import std.core.test as t

def test_ok() {
    t.expect(1 + 1 == 2)
}

def test_broken() {
    t.expect_eq_int(1 + 1, 3)        // deliberate failure
}
