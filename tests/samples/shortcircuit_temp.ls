// Regression: a string temp in a short-circuit operand, when the result lands
// on a POD/bool assignment, was leaked (the bool-assign path reset the temp
// count without freeing). Also guards short-circuit evaluation semantics.
// Self-verifying: prints "SC PASS" only if every check holds.

import std.core.str

def mkstr(int n) -> Str { Str s = "v"; s.push_byte(n + '0'); return s }

int calls = 0
def side() -> bool { calls = calls + 1; return true }

def check(bool cond, int id) -> bool {
    if !cond { @print(id); @print("SC FAIL") }
    return cond
}

def main() {
    bool ok = true

    // && LHS produces a heap string temp; result assigned to a bool.
    bool a = true
    a = mkstr(1).eq?("v1") && a
    if !check(a, 1) { ok = false }

    // || LHS string temp.
    bool b = false
    b = mkstr(2).eq?("nope") || b
    if !check(!b, 2) { ok = false }

    // string temp in the RHS operand.
    bool c = true
    c = c && mkstr(3).eq?("v3")
    if !check(c, 3) { ok = false }

    // nested a && b && c, each operand a string-temp comparison.
    bool d = true
    d = mkstr(1).eq?("v1") && mkstr(2).eq?("v2") && d
    if !check(d, 4) { ok = false }

    // short-circuit semantics must be preserved: RHS not evaluated.
    bool s1 = false && side()      // side() must NOT run
    bool s2 = true || side()       // side() must NOT run
    if !check(calls == 0, 5) { ok = false }
    if !check(!s1, 6) { ok = false }
    if !check(s2, 7) { ok = false }

    // var_decl form (already worked) stays correct.
    bool e = mkstr(9).eq?("v9") && true
    if !check(e, 8) { ok = false }

    if ok { @print("SC PASS") }
}
