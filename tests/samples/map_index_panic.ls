// map_index_panic.ls — reading a missing key via `m[k]` must abort the process
// (panic-on-miss), exactly like Vec's `v[i]` out-of-bounds. The line after the
// bad access must NOT run. Driven by test_map_index.cmake (expects non-zero exit
// + a "key not found" diagnostic, and that "AFTER" is never printed).
import std.map

fn main() -> int {
    Map(Str, int) m = {}
    m["present"] = 1
    int bad = m["absent"]          // missing key -> print + abort()
    print(f"AFTER {bad}")          // must never run
    return 0
}
