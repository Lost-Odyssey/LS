module main

import mod_a
import mod_b

/* Local function with the SAME name as mod_a.read_file / mod_b.read_file.
   Pre-L-009 this collided with the imported @read_file and crashed IR
   verification. The root function must stay unmangled and win for bare calls. */
fn read_file(Str s) -> Str {
    return f"local:{s}"
}

fn main() -> int {
    /* Module-qualified calls must hit each module's own function. */
    int a = mod_a.helper()      // 1
    int b = mod_b.helper()      // 2
    print(f"helper a={a} b={b}")

    /* Bare intra-module calls inside each module resolve to that module. */
    int ca = mod_a.combined()   // 1 + 10 = 11
    int cb = mod_b.combined()   // 2 + 20 = 22
    print(f"combined a={ca} b={cb}")

    /* Same-named Str functions, all distinct. */
    Str la = mod_a.read_file("x")   // a:x
    Str lb = mod_b.read_file("x")   // b:x
    Str ll = read_file("x")         // local:x
    print(f"read {la} {lb} {ll}")

    if a == 1 && b == 2 && ca == 11 && cb == 22 {
        print("L009 PASS")
    } else {
        print("L009 FAIL")
    }
    return 0
}
