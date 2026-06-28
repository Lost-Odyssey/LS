// helper.ls — a module that itself imports std.core.stack and uses Stack(int)
// *internally*. This is the cross-module transitive case: the generic
// container template is registered in this module's checker (via import), the
// instantiation happens here, and its monomorphized methods must bubble up to
// the root and be emitted. Regression for the codegen on-demand method
// forward-declaration fix (generic struct method referenced from a module body
// emitted before the pending-gm block).

module helper

import std.core.stack

def sum_pushed() -> int {
    Stack(int) s = new_stack(int)()
    s.push(10)
    s.push(20)
    s.push(30)
    int total = 0
    while s.is_empty() == false {
        total = total + s.pop()
    }
    return total
}
