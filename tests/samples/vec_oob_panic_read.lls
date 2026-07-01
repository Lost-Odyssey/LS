// vec_oob_panic_read.ls — reading v[i] out of range must abort the process with
// a diagnostic (NOT silently read garbage). The driver asserts a non-zero exit
// code, that stdout contains "out of bounds", and that "AFTER" never prints.
import std.core.vec

def main() -> int {
    Vec(int) v = {}
    v.push(1)
    v.push(2)
    @print("BEFORE")
    int x = v[100]            // out of bounds -> abort
    @print(f"AFTER x={x}")     // must never run
    return 0
}
