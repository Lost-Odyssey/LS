/* Edge cases for split/join. */
import std.vec
import std.string

fn main() -> int {
    /* 1. Empty separator -> single element with full source. */
    Vec(string) v1 = "abc".split("")
    print(v1.len())        /* 1 */
    print(v1[0])            /* abc */

    /* 2. Empty source. */
    Vec(string) v2 = "".split(",")
    print(v2.len())        /* 1 */
    print(v2[0])            /* (empty) */

    /* 3. Separator not present. */
    Vec(string) v3 = "hello".split(",")
    print(v3.len())        /* 1 */
    print(v3[0])            /* hello */

    /* 4. Adjacent separators -> empty parts. */
    Vec(string) v4 = "a,,b".split(",")
    print(v4.len())        /* 3 */
    print(v4[0])            /* a */
    print(v4[1])            /* (empty) */
    print(v4[2])            /* b */

    /* 5. Trailing separator. */
    Vec(string) v5 = "x,y,".split(",")
    print(v5.len())        /* 3 */
    print(v5[2])            /* (empty) */

    /* 6. Multi-char separator. */
    Vec(string) v6 = "one::two::three".split("::")
    print(v6.len())        /* 3 */
    print(v6[1])            /* two */

    /* 7. Round-trip with different separator. */
    Vec(string) v7 = "a,b,c".split(",")
    string r = "-".join(v7)
    print(r)                /* a-b-c */

    /* 8. Join empty vec (build via split of empty). */
    Vec(string) empty = "".split(",")  /* yields [""] not really empty */
    print(",".join(empty))  /* (empty) */

    /* 9. Join single element built via push. */
    Vec(string) one = "solo".split(",")
    print("|".join(one))    /* solo */

    return 0
}
