/* B-5: same-named enum `Res` with SAME variant names `Ok`/`Err` in two imported
   modules. Bare variants resolve by TYPE CONTEXT (declared type / arg type / match
   scrutinee), so `A.Res x = Ok` and `B.Res y = Ok` pick the right enum. No explicit
   `A.Res.Ok` qualified-variant syntax needed (LS's mandatory typing always supplies
   context). */
module main

import mod_a as A
import mod_b as B

def main() -> int {
    /* construction by declared-type context */
    A.Res ra = Ok
    B.Res rb = Ok

    /* argument-type context */
    int a1 = A.code(Ok)      // 1
    int b1 = B.code(Ok)      // 10

    /* match on a qualified-enum value with bare patterns */
    B.Res rb2 = B.good()
    int m = 0
    match rb2 {
        Ok  => { m = 100 }
        Err => { m = 200 }
    }

    @print(f"ra={A.code(ra)} rb={B.code(rb)} a1={a1} b1={b1} m={m}")
    if A.code(ra) == 1 && B.code(rb) == 10 && a1 == 1 && b1 == 10 && m == 100 {
        @print("MODTYPE_ENUM PASS")
    }
    return 0
}
