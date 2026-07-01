/* B-4.1: same-named struct/enum WITH methods in two imported modules. Disambiguated
   via qualified types; impl_registry keyed by type unique name so the two `impl
   Widget` / `impl Kind` don't collide. Instance + static methods + enum methods. */
module main

import mod_a as A
import mod_b as B

def main() -> int {
    A.Widget wa = A.Widget.make(3)   // static, mod_a
    B.Widget wb = B.Widget.make(5)   // static, mod_b
    int va = wa.val()                // 3*10 = 30
    int vb = wb.val()                // 5+100 = 105

    A.Kind ka = A.big()
    B.Kind kb = B.hi()
    int ra = ka.rank()               // Big => 2
    int rb = kb.rank()               // Hi  => 40

    @print(f"va={va} vb={vb} ra={ra} rb={rb}")
    if va == 30 && vb == 105 && ra == 2 && rb == 40 {
        @print("MODTYPE_METHODS PASS")
    }
    return 0
}
