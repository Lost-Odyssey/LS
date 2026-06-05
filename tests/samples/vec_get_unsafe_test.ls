// vec_get_unsafe_test.ls — vec.get_unsafe(i): unchecked index load. Must match
// v[i] / v.get(i) for in-bounds access, for both POD and owned (string) elems.
fn main() -> int {
    int pass = 0
    int fail = 0

    // POD: get_unsafe matches v[i]
    vec(i64) v
    for i in 0..100 { v.push((i * 3) as i64) }
    i64 s_idx = 0
    i64 s_unsafe = 0
    for i in 0..v.length {
        s_idx = s_idx + v[i]
        s_unsafe = s_unsafe + v.get_unsafe(i)
    }
    if s_idx == s_unsafe { pass = pass + 1 } else { fail = fail + 1; print("FAIL pod sum") }
    if v.get_unsafe(50) == 150 { pass = pass + 1 } else { fail = fail + 1; print("FAIL pod elem") }

    // string elements: get_unsafe returns an owned clone (no double-free)
    vec(string) sv
    sv.push("alpha")
    sv.push("beta".upper())
    sv.push("gamma")
    string b = sv.get_unsafe(1)
    if b == "BETA" { pass = pass + 1 } else { fail = fail + 1; print("FAIL str elem") }
    int tot = 0
    for i in 0..sv.length {
        string w = sv.get_unsafe(i)
        tot = tot + w.length
    }
    if tot == 14 { pass = pass + 1 } else { fail = fail + 1; print(f"FAIL str len tot={tot}") }

    print(f"pass={pass} fail={fail}")
    if fail == 0 { print("VEC_GET_UNSAFE PASS") } else { print("VEC_GET_UNSAFE FAIL") }
    return 0
}
