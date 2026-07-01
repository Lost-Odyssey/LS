// A-1 (docs/plan_runtime_primitives.md): canonical-path std.sys.c primitives.
// Verifies std.sys.c.malloc / std.sys.c.realloc / std.sys.c.free are reachable by full
// canonical path — both directly and from a GENERIC method body instantiated
// at this consumer site (the case bare-alias `c.malloc` cannot serve, since the
// alias is torn down before consumer-side monomorphization). abort() is exercised
// for compile/reachability only (an untaken branch — taking it would exit(1)).

import std.core.str

struct Buf(T) { *u8 p; int n }

methods Buf(T) {
    def make(&!self, int n) {
        self.p = std.sys.c.malloc(n)               // canonical malloc in a generic body
        self.n = n
    }
    def grow(&!self, int n) {
        self.p = std.sys.c.realloc(self.p, n)      // canonical realloc in a generic body
        self.n = n
    }
    def release(&!self) {
        std.sys.c.free(self.p)                     // canonical free in a generic body
        self.n = 0
    }
}

def check(bool ok, Str label) {
    if ok { @print(f"  ok: {label}") }
    else  { @print(f"FAIL: {label}") }
}

def main() {
    // ---- direct (non-generic) canonical-path use ----
    *u8 q = std.sys.c.malloc(16)
    q = std.sys.c.realloc(q, 64)
    std.sys.c.free(q)
    check(true, "direct malloc/realloc/free compiled and ran")

    // ---- generic-method canonical-path use (instantiated here) ----
    Buf(int) b = {}
    b.make(8)
    check(b.n == 8, "generic make -> n==8")
    b.grow(32)
    check(b.n == 32, "generic grow -> n==32")
    b.release()
    check(b.n == 0, "generic release -> n==0")

    // ---- abort reachable (untaken) ----
    if false { std.sys.c.abort() }
    check(true, "abort path compiled (untaken)")

    @print("STDCPRIM PASS")
}
