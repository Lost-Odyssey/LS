// guard_literal_reject.ls — you cannot construct a Guard with a struct literal
// that injects your own (still-aliased) data into the private field: setting a
// private field in a literal outside the struct's impl is a compile error. This
// closes the construction-side bypass (the only way in is `{}` + the methods).
import std.core.vec
import std.sync.lock

def main() -> int {
    Vec(int) mine = [9]
    // ✗ would alias `mine` inside the guard, defeating exclusive access.
    Guard(Vec(int)) g = Guard(Vec(int)){ value: mine, handle: __mutex_init() }
    @print(0)
    return 0
}
