// move_only_test.ls — move-only resource types (Destroy + raw ptr/object field,
// no Clone) matched out of an OWNED enum subject.
//
// Before: `match make() { Ok(r) => ... }` on a move-only payload failed to
// compile ("cannot copy ... move-only") because owned-temp match binders are
// cloned. Fix (codegen_match.c): a move-only binder is MOVED out (the subject's
// payload slot is zeroed so its wholesale drop no-ops on it via the destructor's
// nil guard). This is what makes RAII resource handles (io.File, locks, …) work
// with the idiomatic inline `match open(p) { Ok(f) => ... }`.
//
// Self-verifying; the driver runs JIT + AOT + memcheck (single owner: each
// resource is freed exactly once — 0 leak / 0 double-free).
import std.sys.c as c

// A move-only resource: owns a malloc'd buffer, freed once by its destructor.
struct Buf { object p }
methods Buf: Destroy {
    def ~(&!self) {
        if self.p != nil {
            c.free(self.p as *u8)
            self.p = nil
        }
    }
}

def make(bool ok) -> Result(Buf, Str) {
    if !ok { return Err("denied") }
    object mem = c.malloc(16)
    return Ok(Buf { p: mem })
}
def some(bool present) -> Option(Buf) {
    if !present { return None }
    object mem = c.malloc(16)
    return Some(Buf { p: mem })
}
def live(&Buf b) -> bool { return b.p != nil }
def empty() -> Buf { object z = nil; return Buf { p: z } }

def main() -> int {
    // (1) inline match on owned Result rvalue — move-out, single owner
    match make(true) {
        Ok(b)  => { @print(live(&b)) }      // true; b drops (frees) at arm end
        Err(e) => { @print("err") }
    }
    // (2) Err path — no resource constructed
    match make(false) {
        Ok(b)  => { @print(live(&b)) }
        Err(e) => { @print("err") }         // err
    }
    // (3) inline match on owned Option rvalue
    match some(true) {
        Some(b) => { @print(live(&b)) }     // true
        None    => { @print("none") }
    }
    match some(false) {
        Some(b) => { @print(live(&b)) }
        None    => { @print("none") }       // none
    }
    // (4) yield the move-only binder OUT of the match into an outer owner
    Buf kept = match make(true) { Ok(b) => b  Err(e) => empty() }
    @print(live(&kept))                     // true; kept drops once at fn end

    @print("MOVEONLY PASS")
    return 0
}
