// Defining module: a generic struct whose methods call module functions —
// via alias (sc.int_to_hex) and via canonical path (std.text.strconv.int_to_oct).
// The consumer (main.ls) does NOT import std.text.strconv; Phase 2 binds this
// module's import env when the generic method bodies are instantiated there.
import std.text.strconv as sc
struct PBox(T) { T item }
methods(T) PBox(T) {
    def label(&self) -> Str  { return sc.int_to_hex(255) }          // aliased module call
    def label2(&self) -> Str { return std.text.strconv.int_to_oct(8) }   // canonical (Phase 1+2)
}
