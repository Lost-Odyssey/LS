/* B-4: two modules each define `Config` with DIFFERENT layouts; disambiguate via
   module-qualified types `A.Config` / `B.Config` (reusing the `import as` alias +
   `mod.member` grammar, extended to type position). Both coexist correctly. */
module main

import mod_a as A
import mod_b as B

fn main() -> int {
    A.Config ca = A.make()       // mod_a's 1-field Config
    B.Config cb = B.make()       // mod_b's 3-field Config

    int a = A.get(ca)            // 1
    int b = B.sum(cb)            // 60

    print(f"a={a} b={b}")
    if a == 1 && b == 60 {
        print("MODTYPE_QUALIFIED PASS")
    }
    return 0
}
