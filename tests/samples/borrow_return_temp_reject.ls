// Phase 2 (borrow extension): binding a returned borrow whose receiver is a
// TEMPORARY (`make().get()`) must be rejected — the temporary is dropped at
// statement end, dangling the bound borrow. (Immediate use `make().get().field`
// is fine; only the binding escapes the temporary's lifetime.)
import std.str
struct Inner { Str v }
struct Outer { Inner inner }

impl Outer {
    fn get(&self) -> &Inner { return self.inner }
}

fn make() -> Outer { return Outer{inner: Inner{v: "hi"}} }

fn main() -> int {
    &Inner r = make().get()      // receiver is a temporary → must reject
    print(r.v.len())
    return 0
}
