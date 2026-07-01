// Regression: reflecting a type whose deriving module is imported BEFORE
// std.core.reflect. This used to fail with a spurious
//   [type error] ...: unknown type 'TypeInfo'
// because transitive trait propagation re-resolved `interface Reflect`'s
//   static def reflect() -> TypeInfo
// signature in the importer's scope before TypeInfo was bound there.
// Order matters: the deriving module comes first on purpose.
import reflect_imported_mod
import std.core.reflect

@derive(Reflect)
struct Local { int n }

def main() {
    // Reflect an IMPORTED generic type (deriving module imported first).
    TypeInfo ti = Widget(int).reflect()
    @print(ti.name)
    for fi in ti.fields { @print(fi.name + ": " + fi.type_name) }
    // The user destructor must reflect as `~`, not the internal `__drop`.
    for m in ti.funcs { @print(m.signature) }

    // A local deriving type still reflects fine alongside the imported one.
    TypeInfo lt = Local.reflect()
    @print(lt.name)
    @print("REFLECT IMPORTED DONE")
}
