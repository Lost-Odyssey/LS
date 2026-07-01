// Reflecting the foundational containers Vec / Str / Map. Vec and Str sit below
// the str/vec layer, so they derive ReflectRaw (std.core.reflect_core, a leaf)
// and std.core.reflect grafts a friendly reflect() = from_raw(reflect_raw()).
// Map sits above and derives the friendly Reflect directly. All three return the
// same TypeInfo { name, fields, funcs } — fully auto-scanned, no hand-written
// metadata.
import std.core.vec
import std.core.map
import std.core.reflect
import std.core.reflect_core

// A zero-method type exercises RawType.make's nm==0 path (must not leak malloc(0)).
@derive(ReflectRaw)
struct Bare { int a; int b }

def main() {
    TypeInfo b = std.core.reflect.from_raw(Bare.reflect_raw())
    @print("bare:" + b.name)

    TypeInfo v = Vec(int).reflect()
    @print(v.name)
    for fi in v.fields { @print("  " + fi.name + ": " + fi.type_name) }
    bool v_push = false
    bool v_tilde = false
    for m in v.funcs {
        if m.signature.contains?("def push(") { v_push = true }
        if m.signature.contains?("def ~(")    { v_tilde = true }
    }
    if v_push { @print("vec-has-push") }
    if v_tilde { @print("vec-has-tilde") }

    TypeInfo s = Str.reflect()
    @print(s.name)
    for fi in s.fields { @print("  " + fi.name + ": " + fi.type_name) }
    bool s_split = false
    for m in s.funcs {
        if m.signature.contains?("def split(") { s_split = true }
    }
    if s_split { @print("str-has-split") }

    TypeInfo mp = Map(Str, int).reflect()
    @print(mp.name)
    for fi in mp.fields { @print("  " + fi.name + ": " + fi.type_name) }

    @print("REFLECT CONTAINERS DONE")
}
