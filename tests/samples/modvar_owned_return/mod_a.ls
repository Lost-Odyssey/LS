module mod_a

/* OWNED global string (cap > 0 via .upper()), unlike a static "literal" (cap=0).
   The getter returns it by name — pre-fix this double-freed (caller's copy +
   __ls_global_cleanup both freed the shared data pointer). The return path must
   clone because the global retains ownership and is freed at program exit. */
string greeting = "hello".upper()

fn get_greeting() -> string {
    return greeting
}

fn set_greeting(string s) {
    greeting = s
}
