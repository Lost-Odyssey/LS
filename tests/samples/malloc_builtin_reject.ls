// A-FLIP negative test: bare malloc/free/realloc/abort are no longer global
// builtins (they moved to std.c). A bare `malloc(...)` must be rejected as an
// undefined variable — the raw-heap primitives are reached as std.c.malloc / etc.
// See docs/plan_runtime_primitives.md.

fn main() {
    *u8 p = malloc(16)     // ERROR: undefined — use std.c.malloc(16)
    free(p)
}
