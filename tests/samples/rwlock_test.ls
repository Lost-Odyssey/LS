// rwlock_test.ls — std.sync.lock RwLock(T) single-threaded.
// read (shared, read-only &T) + write (exclusive &!T) over Vec/Str/Map/int.
// memcheck 0/0/0. Reader-immutability is enforced by the &T param (a separate
// negative sample checks the rejection).
import std.core.vec
import std.core.str
import std.core.map
import std.sync.lock

RwLock(Vec(int)) g_global = {}

def check(bool cond, Str label) {
    if (cond) { @print(f"PASS {label}") } else { @print(f"RWLOCK FAIL {label}") }
}

def main() -> int {
    RwLock(Vec(int)) data = {}
    data.init()
    data.write(|v| { v.push(1) v.push(2) v.push(3) })
    int n = data.read(int)(|v| { return v.len() })
    check(n == 3, "vec-len")
    int sum = data.read(int)(|v| {
        int acc = 0
        for i in 0..v.len() { acc = acc + v[i] }
        return acc
    })
    check(sum == 6, "vec-sum")

    RwLock(Str) name = {}
    name.init()
    name.write(|s| { s.push_str("hello world") })
    int ln = name.read(int)(|s| { return s.len() })
    check(ln == 11, "str-len")

    RwLock(Map(Str, int)) cfg = {}
    cfg.init()
    cfg.write(|m| { m.set("a", 10) m.set("b", 20) })
    int va = cfg.read(int)(|m| { return m["a"] })
    check(va == 10, "map-read")
    int mlen = cfg.read(int)(|m| { return m.len() })
    check(mlen == 2, "map-len")

    // POD payload (Block(&int) ABI now consistent — pointer)
    RwLock(int) counter = {}
    counter.init()
    counter.write(|x| { x = x + 41 })
    counter.write(|x| { x = x + 1 })
    check(counter.read(int)(|x| { return x }) == 42, "int-rmw")

    // global auto-drop
    g_global.init()
    g_global.write(|v| { v.push(7) })
    check(g_global.read(int)(|v| { return v.len() }) == 1, "global")

    @print("RWLOCK OK")
    return 0
}
