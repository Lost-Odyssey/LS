/* Test: vec(T) v = [..] literal coercion */
fn main() -> int {
    /* int vec from literal */
    vec(int) a = [1, 2, 3]
    print(a.length)        /* 3 */
    print(a[0])            /* 1 */
    print(a[2])            /* 3 */

    /* string vec from literal (static strings) */
    vec(string) s = ["alpha", "beta", "gamma"]
    print(s.length)        /* 3 */
    print(s[1])            /* beta */

    /* string vec with computed elements (owned temps) */
    vec(string) u = ["a".upper(), "b".upper()]
    print(u[0])            /* A */
    print(u[1])            /* B */

    /* Empty vec literal */
    vec(int) e = []
    print(e.length)        /* 0 */
    e.push(42)
    print(e[0])            /* 42 */

    /* Empty string vec */
    vec(string) es = []
    print(es.length)       /* 0 */
    es.push("hello")
    print(es[0])           /* hello */

    /* Mutations after literal init */
    vec(int) m = [10, 20]
    m.push(30)
    print(m.length)        /* 3 */
    print(m[2])            /* 30 */

    return 0
}
