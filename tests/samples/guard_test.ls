// guard_test.ls — std.sync.lock Guard(T) data guard, single-threaded.
// Exercises: exclusive lock/mutate, get(R) read-out, has_drop payloads
// (Vec/Str/Map), Guard as a global (auto __drop at program end). memcheck 0/0/0.
import std.core.vec
import std.core.str
import std.core.map
import std.sync.lock

// Global Guard with has_drop payload — auto-dropped at program end.
Guard(Vec(int)) g_global = {}

def check(bool cond, Str label) {
    if (cond) { @print(f"PASS {label}") } else { @print(f"GUARD FAIL {label}") }
}

def main() -> int {
    // ---- Vec payload: lock-mutate + get read-out ----
    Guard(Vec(int)) data = {}
    data.init()
    data.lock(|v| { v.push(1) })
    data.lock(|v| { v.push(2) })
    data.lock(|v| { v.push(3) })
    int n = data.get(int)(|v| { return v.len() })
    check(n == 3, "vec-len")
    int sum = data.get(int)(|v| {
        int acc = 0
        for i in 0..v.len() { acc = acc + v[i] }
        return acc
    })
    check(sum == 6, "vec-sum")

    // ---- Str payload: has_drop, mutate + copy out ----
    Guard(Str) name = {}
    name.init()
    name.lock(|s| { s.push_str("hello") })
    name.lock(|s| { s.push_str(" world") })
    int ln = name.get(int)(|s| { return s.len() })
    check(ln == 11, "str-len")
    Str copy = name.get(Str)(|s| { return s.copy() })
    check(copy.eq?("hello world"), "str-copy-out")

    // ---- Map payload ----
    Guard(Map(Str, int)) cfg = {}
    cfg.init()
    cfg.lock(|m| { m.set("a", 10) })
    cfg.lock(|m| { m.set("b", 20) })
    int mlen = cfg.get(int)(|m| { return m.len() })
    check(mlen == 2, "map-len")
    int va = cfg.get(int)(|m| { return m["a"] })
    check(va == 10, "map-get")

    // ---- global guard ----
    g_global.init()
    g_global.lock(|v| { v.push(7) })
    g_global.lock(|v| { v.push(8) })
    int gn = g_global.get(int)(|v| { return v.len() })
    check(gn == 2, "global-len")

    @print("GUARD OK")
    return 0
}
