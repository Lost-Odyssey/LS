/* Test vec.get(i) */
fn main() -> int {
    vec(string) v = "a,b,c".split(",")
    print(v.get(0))     /* a */
    print(v.get(1))     /* b */
    print(v.get(2))     /* c */
    print(v.get(99))    /* warning + empty */
    print(v.get(-1))    /* warning + empty */

    vec(int) ints
    ints.push(10)
    ints.push(20)
    ints.push(30)
    print(ints.get(0))  /* 10 */
    print(ints.get(2))  /* 30 */
    print(ints.get(5))  /* warning + 0 */
    return 0
}
