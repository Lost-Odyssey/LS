// vec Batch A end-to-end test: is_empty(), first(), last()
import std.core.vec

def main() -> int {
    // --- is_empty() ---
    Vec(int) v = {}
    bool e1 = v.empty?
    @print(e1)              // true

    v.push(10)
    v.push(20)
    v.push(30)

    bool e2 = v.empty?
    @print(e2)              // false

    // --- first() ---
    match v.first() {
        Some(f) => { @print(f) }   // 10
        None => { @print(0) }
    }

    // --- last() ---
    match v.last() {
        Some(l) => { @print(l) }   // 30
        None => { @print(0) }
    }

    // --- first/last on single-element Vec ---
    Vec(int) single = {}
    single.push(99)
    match single.first() { Some(x) => { @print(x) } None => { @print(0) } }  // 99
    match single.last()  { Some(x) => { @print(x) } None => { @print(0) } }  // 99

    // --- first/last on empty Vec returns None ---
    Vec(int) empty = {}
    match empty.first() { Some(x) => { @print(x) } None => { @print(0) } }  // 0
    match empty.last()  { Some(x) => { @print(x) } None => { @print(0) } }  // 0

    // --- Str Vec: first() and last() return owned clones ---
    Vec(Str) sv = {}
    sv.push("alpha")
    sv.push("beta")
    sv.push("gamma")

    bool se = sv.empty?
    @print(se)               // false

    Str sf = ""
    match sv.first() {
        Some(x) => { sf = x }
        None => { sf = "" }
    }
    @print(sf)               // alpha

    Str sl = ""
    match sv.last() {
        Some(x) => { sl = x }
        None => { sl = "" }
    }
    @print(sl)               // gamma

    // Mutations on the original vec do not affect the clones
    sv.pop()
    @print(sf)               // alpha  (clone, unaffected)
    @print(sl)               // gamma  (clone, unaffected)

    // --- is_empty() after clear ---
    sv.clear()
    bool sc = sv.empty?
    @print(sc)               // true

    return 0
}
