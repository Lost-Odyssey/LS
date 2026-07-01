module mod_a
import std.core.str

/* OWNED global Str (cap > 0 via .upper()), unlike a static "literal" (cap=0).
   The getter returns it by name — pre-fix this double-freed (caller's copy +
   __ls_global_cleanup both freed the shared data pointer). The return path must
   clone because the global retains ownership and is freed at program exit. */
Str greeting = "hello".upper()

def get_greeting() -> Str {
    return greeting
}

def set_greeting(Str s) {
    greeting = s
}
