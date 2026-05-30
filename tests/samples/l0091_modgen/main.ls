module main

import mod_a
import mod_b

/* Root-module generic — must stay unprefixed and work as before. */
fn id(T)(T x) -> T { return x }

fn main() -> int {
    /* A1: generic used inside an imported module is now instantiated. */
    int u = mod_a.use_int()      // box(int)(7) = 7

    /* A2: same generic name, different body across modules — distinct results. */
    int ta = mod_a.run_tag()     // tag => 1
    int tb = mod_b.run_tag()     // tag => 2

    /* Root generic still works. */
    int r = id(int)(42)          // 42

    print(f"u={u} ta={ta} tb={tb} r={r}")
    if u == 7 && ta == 1 && tb == 2 && r == 42 {
        print("L0091 PASS")
    } else {
        print("L0091 FAIL")
    }
    return 0
}
