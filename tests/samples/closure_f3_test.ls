/* Phase F.3 tests: Block as struct fields
   Expected output (one per line):
     10
     15
     factory_ok
     3
     7
*/

import std.str

type Handler = Block(int) -> int
type Factory = Block() -> Str

/* F.3.1: struct with two no-capture Block fields */
struct Pipe {
    Handler step1
    Handler step2
}

/* F.3.2: struct with a capturing Block field (Str by-move) */
struct Maker {
    Factory produce
}

/* F.3.3: struct with Block + Str + POD (multiple has_drop fields) */
struct MultiDrop {
    Str prefix
    Handler transform
    int base
}

fn make_pipe() -> Pipe {
    Pipe p = Pipe { step1: |x| { return x * 2 }, step2: |x| { return x + 10 } }
    return p
}

fn main() {
    /* F.3.1: direct call through struct field */
    Pipe p = make_pipe()
    print(p.step1(5))    /* 10 */
    print(p.step2(5))    /* 15 */

    /* F.3.2: Block field captures Str by-move; struct drop frees env */
    Str tag = "factory_ok"
    Maker m = Maker { produce: [move tag] || { return tag } }
    print(m.produce())   /* factory_ok */

    /* F.3.3: struct with Str + Block + POD — all freed on scope exit */
    MultiDrop md = MultiDrop { prefix: "x", transform: |x| { return x + 1 }, base: 2 }
    print(md.transform(md.base))  /* 3 */

    /* F.3.4: named Block variable moved into struct field */
    Handler add5 = |x| { return x + 5 }
    Pipe p2 = Pipe { step1: add5, step2: |x| { return x * 3 } }
    print(p2.step1(2))   /* 7 */
}   /* p, m, md, p2 all exit — each Block env freed exactly once */
