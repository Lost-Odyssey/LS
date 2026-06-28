// std/core/reflect_core.ls — RAW reflection metadata (the substrate layer).
//
// This is a LEAF module: it depends only on std.sys.c (malloc) and so it can be
// imported by the most foundational types (Str, Vec) WITHOUT an import cycle.
// The friendly std.core.reflect layer — whose TypeInfo is built from Str + Vec —
// cannot be: reflect -> str -> vec, so a type defined below that chain (Str/Vec)
// can never derive the friendly Reflect. It derives ReflectRaw instead, returning
// a RawType; std.core.reflect.from_raw then converts that into a TypeInfo.
//
// Strings are stored as raw `*u8` pointers into baked .rodata (produced by the
// __rawstr compiler intrinsic, NUL-terminated like any C string), so no Str is
// needed here. from_cstr turns them back into owned Str on the friendly side.
//
// Ownership: the fields/funcs arrays are malloc'd by RawType.make. RawType holds
// only raw pointers, so it is NOT has_drop and frees nothing automatically — the
// consumer (from_raw) copies the data out and then calls free_arrays(). The *u8
// strings are baked literals and are never freed.

import std.sys.c as c

struct RawField  { *u8 name; *u8 type_name }
struct RawMethod { *u8 name; *u8 sig; bool is_static }

struct RawType {
    *u8 name
    *RawField  fields
    int        field_count
    *RawMethod funcs
    int        func_count
}

methods RawType {
    // Allocate a RawType with room for `nf` fields and `nm` methods. The caller
    // fills them with set_field / set_method (the derived reflect_raw() does this).
    static def make(*u8 name, int nf, int nm) -> RawType {
        // Only allocate when the count is positive: malloc(0) would return a
        // pointer that free_arrays (guarded on count > 0) never releases — a small
        // but real leak for a zero-field or zero-method type. nil arrays with a
        // 0 count are never indexed (from_raw loops `i < count`).
        *u8 z = nil
        *RawField  ff = z as *RawField
        *RawMethod mm = z as *RawMethod
        if nf > 0 { ff = c.malloc(nf * sizeof(RawField))  as *RawField }
        if nm > 0 { mm = c.malloc(nm * sizeof(RawMethod)) as *RawMethod }
        return RawType { name: name, fields: ff, field_count: nf,
                         funcs: mm, func_count: nm }
    }

    def set_field(&!self, int i, *u8 name, *u8 type_name) {
        self.fields[i] = RawField { name: name, type_name: type_name }
    }

    def set_method(&!self, int i, *u8 name, *u8 sig, bool is_static) {
        self.funcs[i] = RawMethod { name: name, sig: sig, is_static: is_static }
    }

    // Read accessors (used by from_raw; pointer-index loads, no clone).
    def field_at(&self, int i)  -> RawField  { return self.fields[i] }
    def method_at(&self, int i) -> RawMethod { return self.funcs[i] }
}

// RawType is a proper RAII value: a deep clone + an auto destructor, so it is safe
// to copy (copy = move; the compiler rejects use-after-move) and it frees itself
// at scope exit — no manual cleanup contract. The *u8 strings are baked .rodata
// (shared, never freed); only the malloc'd fields/funcs arrays are owned, deep-
// copied by clone and released by ~.
methods RawType: Clone {
    def clone(&self) -> RawType {
        RawType cp = RawType.make(self.name, self.field_count, self.func_count)
        for (int i = 0; i < self.field_count; i = i + 1) { cp.fields[i] = self.fields[i] }
        for (int i = 0; i < self.func_count;  i = i + 1) { cp.funcs[i]  = self.funcs[i] }
        return cp
    }
}

methods RawType: Destroy {
    def ~(&!self) {
        if self.field_count > 0 { c.free(self.fields as *u8) }
        if self.func_count  > 0 { c.free(self.funcs  as *u8) }
    }
}

interface ReflectRaw {
    static def reflect_raw() -> RawType
}
