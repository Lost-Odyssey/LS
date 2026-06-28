// map_index_test.ls — Map(K,V) index protocol: `m[k]` (read, panic-on-miss) and
// `m[k] = v` (write, insert/update). Aligns Map with Vec's `v[i]` three-tier
// model: m[k] is the convenient panic-on-miss accessor, get(k) stays Option.
// Self-verifying; the driver also runs AOT + memcheck (no leak on clones).
import std.core.map
import std.core.str

def check(bool c, Str label) {
    if c { @print(f"  ok: {label}") } else { @print(f"FAIL: {label}") }
}

def main() -> int {
    // ---- m[k] = v inserts; m[k] reads back (POD K/V) ----
    Map(int, int) sq = {}
    for i in 1..6 { sq[i] = i * i }          // index-set inserts
    check(sq[1] == 1 && sq[3] == 9 && sq[5] == 25, "int index get/set")
    check(sq.len() == 5, "len after 5 index-sets")

    // ---- m[k] = v updates an existing key (no duplicate) ----
    sq[3] = 999
    check(sq[3] == 999, "index-set overwrites")
    check(sq.len() == 5, "overwrite does not grow len")

    // ---- m[k] returns V (not Option): usable directly in arithmetic ----
    check(sq[5] + 1 == 26, "m[k] is V, usable in arithmetic")

    // ---- string keys + string values (has_drop), index read clones ----
    Map(Str, Str) names = {}
    names["hi"] = "HELLO"
    check(names["hi"].eq?("HELLO"), "string-key string-value index get")

    Map(Str, int) age = {}
    age["alice"] = 30
    age["bob"] = 25
    check(age["alice"] == 30 && age["bob"] == 25, "string-key index get")
    int a = age["alice"]
    check(a == 30, "bind index read to local")

    // ---- index-set then get(k) (Option path) still agrees ----
    Map(int, int) m = {}
    m[42] = 7
    match m.get(42) {
        Some(v) => check(v == 7, "get() agrees with index-set")
        None    => check(false, "get() should find index-set key")
    }
    check(m.has?(42), "has? after index-set")

    // ---- Vec value (has_drop): index-set moves it in, index-get clones it out;
    //      m[k].len() proves the result is V (a Vec), and chains like v[i] ----
    Map(Str, Vec(int)) lists = {}
    Vec(int) ev = {}
    ev.push(2)
    ev.push(4)
    ev.push(6)
    lists["evens"] = ev                       // moves ev into the map
    check(lists["evens"].len() == 3, "Vec value: m[k] chains .len()")
    Vec(int) got = lists["evens"]             // deep clone out
    check(got[2] == 6, "Vec value index read clones")

    @print("MAP_INDEX PASS")
    return 0
}
