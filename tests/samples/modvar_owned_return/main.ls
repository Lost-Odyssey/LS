import mod_a
import std.str

fn main() -> int {
    /* Bind the returned global string into an owned local — exercises the
       clone-on-return-of-global fix (memcheck must report 0 double-free). */
    Str g = mod_a.get_greeting()
    print(f"g={g}")

    /* Reassign the global to a fresh owned string, then read again. */
    mod_a.set_greeting("world".upper())
    Str h = mod_a.get_greeting()
    print(f"h={h}")

    if g.eq?("HELLO") && h.eq?("WORLD") {
        print("MODVAR_OWNED_RETURN PASS")
    }
    return 0
}
