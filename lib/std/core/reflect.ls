// std/core/reflect.ls — runtime reflection metadata (Stage 3 of
// docs/plan_static_reflection.md). @derive(Reflect) on a struct synthesizes
//   static def reflect() -> TypeInfo
// returning a compile-time-baked description of the type: its fields (name +
// type) and its methods (name + signature + static-ness). opt-in: only types
// that derive Reflect carry this metadata, so non-reflected types pay nothing.

import std.core.str
import std.core.vec
import std.core.reflect_core

struct FieldInfo  { Str name; Str type_name }
struct MethodInfo { Str name; Str signature; bool is_static }
// NOTE: the method list field is named `funcs`, not `methods` — `methods` is a
// language keyword and cannot be used as a field name (`ti.methods` won't parse).
struct TypeInfo   { Str name; Vec(FieldInfo) fields; Vec(MethodInfo) funcs }

interface Reflect {
    static def reflect() -> TypeInfo
}

// Bridge the raw (substrate) metadata into a friendly TypeInfo. Foundational
// types (Str, Vec) below the str/vec layer cannot derive the friendly Reflect
// (it would import str -> vec -> itself), so they derive ReflectRaw and expose
// reflect() = from_raw(reflect_raw()). from_raw copies the *u8 names back into
// owned Str (via from_cstr) and then releases the raw arrays.
def from_raw(RawType rt) -> TypeInfo {
    Vec(FieldInfo) fs = []
    for (int i = 0; i < rt.field_count; i = i + 1) {
        RawField rf = rt.field_at(i)
        fs.push(FieldInfo { name: from_cstr(rf.name as object),
                            type_name: from_cstr(rf.type_name as object) })
    }
    Vec(MethodInfo) ms = []
    for (int i = 0; i < rt.func_count; i = i + 1) {
        RawMethod rm = rt.method_at(i)
        ms.push(MethodInfo { name: from_cstr(rm.name as object),
                             signature: from_cstr(rm.sig as object),
                             is_static: rm.is_static })
    }
    Str nm = from_cstr(rt.name as object)
    return TypeInfo { name: nm, fields: fs, funcs: ms }
}

// Friendly reflect() grafted onto the foundational types from ABOVE the str/vec
// layer (here, where TypeInfo/Str/Vec all exist). They derive ReflectRaw down in
// their own modules (co-located with their methods, so the method scan is fully
// automatic); these grafts just bridge the raw metadata to a TypeInfo.
methods Str: Reflect {
    static def reflect() -> TypeInfo { return from_raw(Str.reflect_raw()) }
}

// Vec's reflected shape is independent of T: its fields are `data: *T`, `len`,
// `cap` and its method signatures carry the abstract `T`, never a concrete type.
// So we reflect it ONCE here through a concrete placeholder instance (Vec(int)),
// in this non-generic function checked in reflect.ls's own scope (where Vec /
// RawType / from_raw are all bound). The generic graft below just delegates to it
// via the canonical path, so its body never names Vec(T) or RawType — which keeps
// it resolvable when re-checked at each Vec(U) instantiation in a consumer scope.
def reflect_vec() -> TypeInfo {
    return from_raw(Vec(int).reflect_raw())
}

methods(T) Vec(T): Reflect {
    static def reflect() -> TypeInfo { return std.core.reflect.reflect_vec() }
}
