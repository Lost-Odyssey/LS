/* Phase F.2 tests: Block assignment + move semantics
   Expected output (one per line):
     6
     107
     8
     9
     factory_ok
*/

import std.str

type F1 = Block(int) -> int
type F0 = Block() -> Str

/* F.2.1: simple Block assignment — g moved to h, h works, g.env_ptr nulled */
fn test_f2_1() -> int {
    F1 g = |x| { return x + 1 }
    F1 h = g
    return h(5)
}

/* F.2.2: Block with POD capture, assignment transfers env */
fn test_f2_2() -> int {
    int base = 100
    F1 f1 = |x| { return x + base }
    F1 f2 = f1
    return f2(7)
}

/* F.2.3: Block re-assignment (g assigned twice with different closures) */
fn test_f2_3() -> int {
    F1 p = |x| { return x * 2 }
    F1 q = p
    return q(4)
}

/* F.2.4: no-capture Block move (env is NULL — just fn_ptr copy) */
fn test_f2_4() -> int {
    F1 noc1 = |x| { return x * 3 }
    F1 noc2 = noc1
    return noc2(3)
}

/* F.2.5: Block returned from factory by named variable */
fn make_greeter() -> F0 {
    Str tag = "factory_ok"
    F0 g = [move tag] || { return tag }
    return g
}

fn main() {
    print(test_f2_1())    /* 6 */
    print(test_f2_2())    /* 107 */
    print(test_f2_3())    /* 8 */
    print(test_f2_4())    /* 9 */

    F0 gr = make_greeter()
    print(gr())            /* factory_ok */
}
