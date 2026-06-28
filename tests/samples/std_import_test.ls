/* std_import_test.ls — `import std.core.math` is the canonical path for the
   merged math module (built-in primitives + LS-derived helpers). `perf` is
   still a bare built-in module. */

import std.core.math as math
import perf

def main() {
    /* std.core.math */
    f64 r = math.sqrt(4.0)
    @print(r)                  /* 2 */

    f64 p = math.pow(2.0, 10.0)
    @print(p)                  /* 1024 */

    /* perf */
    i64 t0 = perf.now()
    i64 dt = perf.elapsed_ns(t0)
    bool ok = dt >= 0
    @print(ok)                 /* true */

    @print("ALL PASS")
}
