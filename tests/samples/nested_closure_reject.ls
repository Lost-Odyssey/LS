// Closure-foundation Phase B — negative: transitive by-move capture is rejected.
//
// `msg` is a function-scope Str (has_drop). An inner closure references it across
// the enclosing closure boundary — a transitive by-move capture, which v1 does
// not support (threading owned heap through multiple env layers is double-free
// prone; deferred to v2). The compiler must reject this with a clear message.
//
// Expected: compile FAILED, stderr mentions "transitive by-move".

import std.str

type NullFn = Block() -> int

fn main() {
    Str msg = "hello"
    NullFn outer = || {
        NullFn inner = || msg.len()   // msg: transitive by-move -> rejected
        return inner()
    }
    print(outer())
}
