// spinguard_test.ls — std.sync.lock SpinGuard(T) single-threaded.
// A Guard backed by the bare adaptive SpinLock (busy-wait) instead of the OS
// mutex. Same data-guard guarantee; no init() / no __drop. Struct + POD payloads.
import std.core.vec
import std.core.str
import std.sync.lock

SpinGuard(int) g_counter = {}

def check(bool cond, Str label) {
    if (cond) { @print(f"PASS {label}") } else { @print(f"SPINGUARD FAIL {label}") }
}

def main() -> int {
    // POD payload (no init needed — flag starts free)
    SpinGuard(int) ctr = {}
    ctr.lock(|x| { x = x + 41 })
    ctr.lock(|x| { x = x + 1 })
    check(ctr.get(int)(|x| { return x }) == 42, "int-rmw")

    // struct payload
    SpinGuard(Vec(int)) sv = {}
    sv.lock(|v| { v.push(1) v.push(2) v.push(3) })
    check(sv.get(int)(|v| { return v.len() }) == 3, "vec-len")
    int sum = sv.get(int)(|v| {
        int a = 0
        for i in 0..v.len() { a = a + v[i] }
        return a
    })
    check(sum == 6, "vec-sum")

    // has_drop payload
    SpinGuard(Str) name = {}
    name.lock(|s| { s.push_str("spin") })
    check(name.get(int)(|s| { return s.len() }) == 4, "str")

    // global auto-drop (value drops; no handle)
    g_counter.lock(|x| { x = x + 7 })
    check(g_counter.get(int)(|x| { return x }) == 7, "global")

    @print("SPINGUARD OK")
    return 0
}
