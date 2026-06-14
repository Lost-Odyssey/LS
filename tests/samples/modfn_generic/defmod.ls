// Defining module: a generic struct whose methods call module functions —
// via alias (sc.int_to_hex) and via canonical path (std.strconv.int_to_oct).
// The consumer (main.ls) does NOT import std.strconv; Phase 2 binds this
// module's import env when the generic method bodies are instantiated there.
import std.strconv as sc
struct PBox(T) { T item }
impl(T) PBox(T) {
    fn label(&self) -> Str  { return sc.int_to_hex(255) }          // aliased module call
    fn label2(&self) -> Str { return std.strconv.int_to_oct(8) }   // canonical (Phase 1+2)
}
