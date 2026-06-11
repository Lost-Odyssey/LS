// vec_api_test.ls — comprehensive Vec(T) API parity with builtin vec.
// Exercises every method on int (POD) and Str (has_drop), incl. move-out
// (pop/remove/swap/reverse via __take) and search (index_of/contains/count).
// memcheck must be 0/0/0. Prints "ok <l>" / "FAIL <l>" then "API PASS".

import std.vec
import std.str

fn check(bool c, Str l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn main() {
    // ───────────────── int (POD) ─────────────────
    Vec(int) v = [10, 20, 30]
    check(v.len() == 3, "len 3")
    check(!v.empty?, "not empty")
    check(v.get(1) == 20, "get(1)=20")
    check(v[1] == 20, "index read")
    v[1] = 25
    check(v.get(1) == 25 && v[1] == 25, "index set")

    v.push(40)
    check(v.len() == 4 && v.get(3) == 40, "push 40")

    v.insert(0, 5)                       // [5,10,25,30,40]
    check(v.get(0) == 5 && v.get(1) == 10 && v.len() == 5, "insert front")
    v.insert(2, 15)                      // [5,10,15,25,30,40]
    check(v.get(2) == 15 && v.get(3) == 25 && v.len() == 6, "insert middle")

    int rem = v.remove(0)                // -> 5, [10,15,25,30,40]
    check(rem == 5 && v.get(0) == 10 && v.len() == 5, "remove front")
    int rem2 = v.remove(1)               // -> 15, [10,25,30,40]
    check(rem2 == 15 && v.get(1) == 25 && v.len() == 4, "remove middle")

    v.swap(0, 3)                         // [40,25,30,10]
    check(v.get(0) == 40 && v.get(3) == 10, "swap")

    v.reverse()                          // [10,30,25,40]
    check(v.get(0) == 10 && v.get(1) == 30 && v.get(3) == 40, "reverse")

    check(v.has?(25), "contains 25")
    check(!v.has?(99), "not contains 99")
    check(v.index_of(25) == 2, "index_of 25")
    check(v.count_eq(25) == 1, "count_eq 25")

    match v.first() { Some(x) => { check(x == 10, "first=10") } None => { check(false, "first") } }
    match v.last()  { Some(x) => { check(x == 40, "last=40") } None => { check(false, "last") } }

    v.truncate(2)                        // [10,30]
    check(v.len() == 2 && v.get(1) == 30, "truncate")

    v.resize(5, 0)                       // [10,30,0,0,0]
    check(v.len() == 5 && v.get(4) == 0, "resize grow")
    v.resize(1, 0)                       // [10]
    check(v.len() == 1 && v.get(0) == 10, "resize shrink")

    Vec(int) c = v.copy()
    c.push(99)
    check(c.len() == 2 && v.len() == 1, "copy is independent")

    v.clear()
    check(v.empty?, "clear")
    v.shrink_to_fit()
    check(v.cap() == 0, "shrink empty -> cap 0")

    // ───────────────── Str (has_drop) ─────────────────
    Vec(Str) s = [f"a", f"b", f"c", f"d"]
    check(s.len() == 4, "str len 4")
    check(s[1].eq?("b"), "str index read")
    s[1] = f"B"
    check(s.get(1).eq?("B") && s[1].eq?("B"), "str index set")
    check(s.has?("B"), "str contains B")
    check(s.index_of("c") == 2, "str index_of c")
    check(s.count_eq("missing") == 0, "str count_eq missing")

    s.insert(1, f"X")                    // [a,X,b,c,d]
    check(s.get(1).eq?("X") && s.get(2).eq?("B"), "str insert")

    Str r = s.remove(0)                  // -> a, [X,b,c,d]
    check(r.eq?("a") && s.get(0).eq?("X"), "str remove (move-out)")

    s.swap(0, 3)                         // [d,b,c,X]
    check(s.get(0).eq?("d") && s.get(3).eq?("X"), "str swap")

    s.reverse()                          // [X,c,b,d]
    check(s.get(0).eq?("X") && s.get(3).eq?("d"), "str reverse")

    s.set(0, f"NEW")                     // drop X, [NEW,c,b,d]
    check(s.get(0).eq?("NEW"), "str set")

    match s.pop() { Some(x) => { check(x.eq?("d"), "str pop (move-out)") } None => { check(false, "str pop") } }
    check(s.len() == 3, "str len after pop")

    s.resize(5, f"fill")                 // [NEW,c,b,fill,fill]
    check(s.len() == 5 && s.get(4).eq?("fill"), "str resize grow (clone fill)")
    s.truncate(2)                        // [NEW,c]
    check(s.len() == 2, "str truncate")

    Vec(Str) sc = s.copy()
    check(sc.len() == 2 && sc.get(1).eq?("c"), "str copy")

    print("API PASS")
    // scope exit: v (empty), c, s, sc all drop their remaining elements + buffers.
}
