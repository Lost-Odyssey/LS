// Regression module for the has_user_clone forward-declaration fix:
// a module function that clones OWNED Str values (f-string / substr products)
// into a Map. Before the fix, emit_struct_clone_val could not find
// std_str__Str.__clone while emitting THIS module (std.core.str's body emits
// later), silently fell back to field-wise auto-clone (= shallow copy of the
// raw *u8 buffer) -> double-free / heap corruption at scope exit.
import std.core.map
import std.core.str

def build(int n) -> Map(Str, Str) {
    Map(Str, Str) m = {}
    for (int i = 0; i < n; i = i + 1) {
        Str k = f"key{i}"
        Str v = f"val{i}"
        m.set(k, v)
    }
    return m
}

def pick(int n) -> Str {
    Map(Str, Str) m = build(n)
    Str probe = "key3"
    Str hit = m.get(probe).unwrap_or("miss")
    return hit
}
