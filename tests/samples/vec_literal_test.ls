/* Test: Vec(T) v = [..] literal coercion */
import std.vec

fn main() -> int {
    /* int vec from literal */
    Vec(int) a = [1, 2, 3]
    print(a.len())         /* 3 */
    print(a[0])            /* 1 */
    print(a[2])            /* 3 */

    /* Str vec from literal (static strings) */
    Vec(Str) s = ["alpha", "beta", "gamma"]
    print(s.len())         /* 3 */
    print(s[1])            /* beta */

    /* Str vec with computed elements (owned temps) */
    Vec(Str) u = ["a".upper(), "b".upper()]
    print(u[0])            /* A */
    print(u[1])            /* B */

    /* Empty vec literal */
    Vec(int) e = {}
    print(e.len())         /* 0 */
    e.push(42)
    print(e[0])            /* 42 */

    /* Empty Str vec */
    Vec(Str) es = {}
    print(es.len())        /* 0 */
    es.push("hello")
    print(es[0])           /* hello */

    /* Mutations after literal init */
    Vec(int) m = [10, 20]
    m.push(30)
    print(m.len())         /* 3 */
    print(m[2])            /* 30 */

    return 0
}
