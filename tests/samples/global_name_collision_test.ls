// Regression: a global variable whose NAME matches a compiler-internal private
// constant (printf format ".ls.fmt", string literal ".ls.strlit", __rawstr
// ".ls.rawstr") must not collide. Before the fix those internal constants used
// bare hints "fmt"/"Strlit"/"rawstr"; the user global was auto-renamed by LLVM
// (fmt -> fmt.N) while the name-based global init-store + __ls_global_cleanup
// still resolved the bare name to the internal .rodata constant — storing a Str
// into / destroying a format string => heap corruption + "invalid free".
//
// `fmt`  : call-initialized global (runtime init in __ls_global_stmts) — the
//          path that segfaulted (store into format constant + ~ on .rodata).
// `Strlit`: literal-initialized global (constant-init path) — the path that
//          failed `Global variable initializer type does not match`.
// `rawstr`: a third collider, call-initialized.
import std.core.str

def make(Str s) -> Str { return s }

Str fmt = make("F")
Str Strlit = "S"
Str rawstr = make("R")

@print(fmt)
@print(Strlit)
@print(rawstr)
@print("OK")
