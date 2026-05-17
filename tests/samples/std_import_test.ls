/* std_import_test.ls — Verify that `import std.math` and `import std.perf`
   work identically to `import math` and `import perf`. */

import std.math as math
import std.perf as perf

fn main() {
    /* std.math */
    f64 r = math.sqrt(4.0)
    print(r)                  /* 2 */

    f64 p = math.pow(2.0, 10.0)
    print(p)                  /* 1024 */

    /* std.perf */
    i64 t0 = perf.now()
    i64 dt = perf.elapsed_ns(t0)
    bool ok = dt >= 0
    print(ok)                 /* true */

    print("ALL PASS")
}
