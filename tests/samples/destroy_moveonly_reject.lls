// negative (方式一 move-only safety): a struct with a destructor (~) owning a raw
// pointer, but NO Clone, is move-only — cloning it must be a compile error (not a
// silent double-free).
import std.sys.c as c
struct Buf { *u8 data }
methods Buf: Destroy {
    def ~(&!self) { c.free(self.data) }
}
def main() -> int {
    Vec(Buf) v = {}
    *u8 z = nil
    Buf b = Buf{ data: c.realloc(z, 16) as *u8 }
    v.push(b)
    Buf c2 = v[0]    // clone of move-only Buf -> COMPILE ERROR
    return 0
}
