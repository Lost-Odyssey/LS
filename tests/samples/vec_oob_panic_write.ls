// vec_oob_panic_write.ls — writing out of range (v[i] = x / set) must abort with
// a diagnostic instead of corrupting the heap at a bogus address.
import std.core.vec

def main() -> int {
    Vec(int) v = {}
    v.push(1)
    v.push(2)
    @print("BEFORE")
    v[100] = 7                // out of bounds -> abort
    @print("AFTER")            // must never run
    return 0
}
