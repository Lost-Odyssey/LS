// set_test.ls — std.core.set correctness + ownership/memcheck probe.
//
// Prints "ok <label>" per passing check, "FAIL <label>" on mismatch, then
// "SET PASS" at the end. The driver (test_set.cmake) asserts SET PASS is
// present, FAIL is absent, and (under --memcheck) 0 leak / 0 double-free.

import std.core.set
import std.core.str

def check(bool cond, Str label) {
    if cond {
        @print(f"ok {label}")
    } else {
        @print(f"FAIL {label}")
    }
}

def main() {
    // ---- Set(int): POD element ----
    Set(int) si = {}
    check(si.empty?, "int empty init")
    check(si.len() == 0, "int len 0")

    check(si.insert(1), "int insert 1 new")
    check(si.insert(2), "int insert 2 new")
    check(si.insert(3), "int insert 3 new")
    check(si.insert(2) == false, "int insert 2 dup -> false")
    check(si.len() == 3, "int len 3 (dup ignored)")

    check(si.has?(2), "int has 2")
    check(si.contains(3), "int contains 3")
    check(si.has?(9) == false, "int has 9 false")

    check(si.remove(2), "int remove 2 -> true")
    check(si.has?(2) == false, "int 2 gone")
    check(si.remove(2) == false, "int remove 2 again -> false")
    check(si.len() == 2, "int len 2 after remove")

    si.clear()
    check(si.empty?, "int empty after clear")
    check(si.len() == 0, "int len 0 after clear")

    // ---- Set(Str): has_drop element ----
    Set(Str) ss = {}
    check(ss.insert("alpha"), "str insert alpha new")
    check(ss.insert("beta"), "str insert beta new")
    check(ss.insert("alpha") == false, "str insert alpha dup -> false")
    check(ss.len() == 2, "str len 2")
    check(ss.has?("beta"), "str has beta")
    check(ss.has?("gamma") == false, "str has gamma false")

    // remove drops the stored Str in place.
    check(ss.remove("alpha"), "str remove alpha")
    check(ss.len() == 1, "str len 1 after remove")

    // ss still owns "beta": on scope exit the set drops its Map(Str,bool),
    // which must free the remaining string exactly once (memcheck probe for
    // generic struct + Map(T,_) field drop).

    // ---- list literal (de-dup) + iteration ----
    Set(int) lit = [1, 2, 3, 3, 2, 5]
    check(lit.len() == 4, "literal de-dup -> {1,2,3,5}")
    check(lit.has?(5), "literal has 5")
    check(lit.has?(4) == false, "literal lacks 4")

    // for-in yields each element once; sum the distinct elements.
    int sum = 0
    int cnt = 0
    for x in lit {
        sum = sum + x
        cnt = cnt + 1
    }
    check(sum == 11, "for-in sum 1+2+3+5 = 11")
    check(cnt == 4, "for-in visited 4 elements")

    // to_vec collects all elements (slot order).
    Vec(int) v = lit.to_vec()
    check(v.len() == 4, "to_vec len 4")

    // ---- Set(Str) literal + iteration (has_drop) ----
    Set(Str) ws = ["red", "green", "red", "blue"]
    check(ws.len() == 3, "str literal de-dup -> 3")
    int wlen = 0
    for w in ws {
        wlen = wlen + w.len()
    }
    check(wlen == 12, "str for-in total chars red+green+blue = 12")

    // ---- set algebra ----
    Set(int) a = [1, 2, 3]
    Set(int) b = [2, 3, 4]

    Set(int) u = a.union(&b)
    check(u.len() == 4, "union len 4")
    check(u.has?(1) && u.has?(4), "union has 1 and 4")

    Set(int) ix = a.intersect(&b)
    check(ix.len() == 2, "intersect len 2")
    check(ix.has?(2) && ix.has?(3), "intersect = {2,3}")
    check(ix.has?(1) == false, "intersect lacks 1")

    Set(int) d = a.difference(&b)
    check(d.len() == 1, "difference len 1")
    check(d.has?(1) && d.has?(4) == false, "difference = {1}")

    // operator sugar: + = union, - = difference
    Set(int) up = a + b
    check(up.len() == 4, "a + b union len 4")
    Set(int) dp = a - b
    check(dp.len() == 1 && dp.has?(1), "a - b difference = {1}")

    // predicates
    Set(int) sub = [2, 3]
    check(sub.is_subset(&a), "{2,3} subset of {1,2,3}")
    check(a.is_superset(&sub), "{1,2,3} superset of {2,3}")
    check(a.is_subset(&b) == false, "{1,2,3} not subset of {2,3,4}")
    Set(int) far = [7, 8]
    check(a.is_disjoint(&far), "{1,2,3} disjoint {7,8}")
    check(a.is_disjoint(&b) == false, "{1,2,3} not disjoint {2,3,4}")

    // extend mutates in place
    Set(int) g = [1, 2]
    g.extend(&far)
    check(g.len() == 4, "extend -> len 4")

    @print("SET PASS")
}
