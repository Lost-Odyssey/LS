/* Test: temporary strings in condition expressions are properly freed.
   Before the fix, f"..." and Str concatenations in if/while/for(;;) conditions
   leaked on every evaluation.
   Expected output:
     match0
     match1
     match2
     while ok
     for ok
     done
*/

def main() {
    /* T1: fstring in if-condition (3 iterations) */
    int i = 0
    while i < 3 {
        Str s = f"match{i}"
        if s == f"match{i}" {
            @print(s)
        }
        i = i + 1
    }

    /* T2: fstring in while-condition */
    Str cur = "start"
    int n = 0
    while cur != f"end{n}" {
        cur = f"end{n}"
    }
    @print("while ok")

    /* T3: Str concatenation in for-C condition */
    int k = 0
    Str prefix = "x"
    for (k = 0; prefix + "0" != "x0"; k = k + 1) {
        /* condition false from first check — body never executes */
    }
    @print("for ok")

    @print("done")
}
