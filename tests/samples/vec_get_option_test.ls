// vec_get_option_test.ls — `Vec.get(i) -> Option(T)` recoverable read.
// Ownership sweep for the Option-returning get: POD, owned Str, has_drop struct,
// nested Vec payloads, through match / combinators / force-unwrap / try.
// Negative OOB-panic coverage for v[i] stays in vec_oob_panic_*.ls.
import std.core.vec
import std.core.str

struct Kid { Str name; int age }

// Option in a Result-returning def: `try get(i).ok_or(...)` one-line propagation.
def pick(&Vec(Str) v, int i) -> Result(Str, Str) {
    Str s = try v.get(i).ok_or("index out of range")
    return Ok(s)
}

def main() -> int {
    int pass = 0
    int fail = 0

    // ---- POD: Some / None on both sides, combinators, force-unwrap ----
    Vec(int) v = {}
    for i in 0..5 { v.push(i * 10) }
    if v.get(2).unwrap_or(-1) == 20 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL pod some") }
    if v.get(5).is_none?() && v.get(-1).is_none?() { pass = pass + 1 } else { fail = fail + 1; @print("FAIL pod none") }
    if v.get(4)! == 40 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL pod unwrap") }
    match v.get(0) {
        Some(x) => { if x == 0 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL pod match val") } }
        None    => { fail = fail + 1; @print("FAIL pod match none") }
    }

    // ---- owned Str payload: clone out, source stays usable; None path no leak ----
    Vec(Str) sv = {}
    sv.push("alpha")
    sv.push("beta")
    Str a = sv.get(0)!                       // move payload out of rvalue Option
    if a.eq?("alpha") && sv.len() == 2 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL str unwrap") }
    match sv.get(1) {
        Some(s) => { if s.eq?("beta") { pass = pass + 1 } else { fail = fail + 1; @print("FAIL str match") } }
        None    => { fail = fail + 1; @print("FAIL str match none") }
    }
    if sv.get(7).is_none?() { pass = pass + 1 } else { fail = fail + 1; @print("FAIL str oob") }
    Str fb = sv.get(9).unwrap_or("fallback") // owned fallback path
    if fb.eq?("fallback") { pass = pass + 1 } else { fail = fail + 1; @print("FAIL str unwrap_or") }
    if sv[0].eq?("alpha") { pass = pass + 1 } else { fail = fail + 1; @print("FAIL str source intact") }

    // ---- try + ok_or propagation ----
    match pick(sv, 1) {
        Ok(s)  => { if s.eq?("beta") { pass = pass + 1 } else { fail = fail + 1; @print("FAIL try ok") } }
        Err(e) => { fail = fail + 1; @print("FAIL try err") @print(e) }
    }
    match pick(sv, 42) {
        Ok(s)  => { fail = fail + 1; @print("FAIL try miss ok") @print(s) }
        Err(e) => { if e.eq?("index out of range") { pass = pass + 1 } else { fail = fail + 1; @print("FAIL try miss msg") } }
    }

    // ---- has_drop struct payload ----
    Vec(Kid) kids = {}
    kids.push(Kid { name: "ann", age: 7 })
    kids.push(Kid { name: "bob", age: 9 })
    match kids.get(1) {
        Some(k) => { if k.name.eq?("bob") && k.age == 9 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL kid match") } }
        None    => { fail = fail + 1; @print("FAIL kid none") }
    }
    Kid kc = kids.get(0)!
    if kc.name.eq?("ann") && kids.len() == 2 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL kid unwrap") }
    if kids.get(2).is_none?() { pass = pass + 1 } else { fail = fail + 1; @print("FAIL kid oob") }

    // ---- nested Vec payload: Option(Vec(int)) ----
    Vec(Vec(int)) rows = {}
    Vec(int) r0 = [1, 2, 3]
    rows.push(r0)
    match rows.get(0) {
        Some(row) => { if row.len() == 3 && row[1] == 2 { pass = pass + 1 } else { fail = fail + 1; @print("FAIL row match") } }
        None      => { fail = fail + 1; @print("FAIL row none") }
    }
    if rows.get(3).is_none?() { pass = pass + 1 } else { fail = fail + 1; @print("FAIL row oob") }

    @print(f"pass={pass} fail={fail}")
    if fail == 0 { @print("VEC_GET_OPTION PASS") } else { @print("VEC_GET_OPTION FAIL") }
    return 0
}
