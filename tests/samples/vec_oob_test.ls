// vec_oob_test.ls — in-range access for all four forms must behave identically:
//   v[i] / v.get(i)  (bounds-checked)  and  v.get!(i) / v.set!(i,x)  (unchecked).
// Out-of-range panic is covered by the negative samples vec_oob_panic_*.ls.
import std.vec

fn main() -> int {
    int pass = 0
    int fail = 0

    Vec(int) v = {}
    for i in 0..10 { v.push(i) }

    // checked read
    if v.get(3) == 3 { pass = pass + 1 } else { fail = fail + 1; print("FAIL get") }
    if v[7] == 7 { pass = pass + 1 } else { fail = fail + 1; print("FAIL index read") }

    // checked write (set + v[i]=x)
    v.set(3, 99)
    if v[3] == 99 { pass = pass + 1 } else { fail = fail + 1; print("FAIL set") }
    v[5] = 55
    if v.get(5) == 55 { pass = pass + 1 } else { fail = fail + 1; print("FAIL index write") }

    // unchecked forms match the checked ones for in-range indices
    v.set!(4, 44)
    if v.get!(4) == 44 { pass = pass + 1 } else { fail = fail + 1; print("FAIL get!/set!") }

    // owned (string) elements: checked + unchecked clones, no double-free
    Vec(string) sv = {}
    sv.push("alpha")
    sv.push("beta")
    string a = sv.get(0)
    string b = sv.get!(1)
    if a == "alpha" && b == "beta" { pass = pass + 1 } else { fail = fail + 1; print("FAIL str") }
    sv.set(0, "ALPHA")
    if sv[0] == "ALPHA" { pass = pass + 1 } else { fail = fail + 1; print("FAIL str set") }

    // in-range mutators (insert/remove/swap/truncate/resize) must behave
    Vec(int) m = {}
    for i in 0..5 { m.push(i * 10) }      // [0,10,20,30,40]
    m.insert(0, 100)                       // boundary low: [100,0,10,20,30,40]
    m.insert(m.len(), 200)                 // boundary high (i==len, append)
    if m[0] == 100 && m[m.len() - 1] == 200 { pass = pass + 1 } else { fail = fail + 1; print("FAIL insert") }
    int r = m.remove(0)                    // boundary low
    if r == 100 && m[0] == 0 { pass = pass + 1 } else { fail = fail + 1; print("FAIL remove") }
    m.swap(0, m.len() - 1)                 // boundary both ends
    if m[0] == 200 { pass = pass + 1 } else { fail = fail + 1; print("FAIL swap") }
    m.truncate(2)
    if m.len() == 2 { pass = pass + 1 } else { fail = fail + 1; print("FAIL truncate") }
    m.resize(4, 7)
    if m.len() == 4 && m[3] == 7 { pass = pass + 1 } else { fail = fail + 1; print("FAIL resize") }

    print(f"pass={pass} fail={fail}")
    if fail == 0 { print("VEC_OOB PASS") } else { print("VEC_OOB FAIL") }
    return 0
}
