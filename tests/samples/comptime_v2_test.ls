// comptime field iteration — v2: generic construction + writable fields,
// enum variants(T), and f.type as a type value.
//   ① T{} / T{...} build a value of a type-param type; v.(f) = x writes a field
//      (POD and owned Str), so a body can CONSTRUCT and not just read.
//   ② comptime for vr in variants(T) unrolls per enum variant
//      (vr.name / vr.index / vr.type_name).
//   ③ f.type lowers to the field's concrete type — chiefly a static-call receiver
//      f.type.from_value(x), giving write-once deserialize with no @derive.
// JIT + AOT + memcheck 0/0/0.

import std.core.str
import std.core.value

struct Point { int x; int y }
struct Rec   { Str name; int n; bool ok }

@derive(Serialize)
struct Conf { int port; bool tls; Str host }

enum Shape { Circle(int); Square(int); Empty }

// ① generic construction: build a fresh T, fill each field (transform).
def doubled(T)(&T src) -> T {
    T out = T{}
    comptime for f in fields(T) {
        comptime if f.type_name == "int" { out.(f) = src.(f) + src.(f) }
    }
    return out
}

// ① write-once field-wise copy through a borrow (owned Str field clones).
def copy_all(T)(&T src) -> T {
    T out = T{}
    comptime for f in fields(T) {
        out.(f) = src.(f)
    }
    return out
}

// ① writable in-place through &!T (POD + owned fields both reset).
def blank(T)(&!T v) {
    comptime for f in fields(T) {
        comptime if f.type_name == "Str" { v.(f) = "" }
        comptime if f.type_name == "int" { v.(f) = 0 }
        comptime if f.type_name == "bool" { v.(f) = false }
    }
}

// ② variants(T): metadata over enum variants (name / index / payload type).
def variant_info(T)() -> Str {
    Str out = ""
    comptime for vr in variants(T) {
        out = out + vr.name + "#" + f"{vr.index}"
        comptime if vr.type_name == "" {
            out = out + "() "
        } else {
            out = out + "(" + vr.type_name + ") "
        }
    }
    return out
}

// ③ f.type.from_value: write-once generic deserialize (no @derive(Deserialize)).
def from_value_generic(T)(Value v) -> T {
    T out = T{}
    comptime for f in fields(T) {
        out.(f) = f.type.from_value(std.core.value.obj_get(v, f.name))
    }
    return out
}

def main() -> int {
    // ① construct + transform
    Point p = Point { x: 3, y: 5 }
    Point d = doubled(p)
    @print(f"doubled={d.x},{d.y}")              // doubled=6,10

    // ① field-wise copy with an owned Str field; source stays intact
    Rec a = Rec { name: "alice", n: 42, ok: true }
    Rec b = copy_all(a)
    @print(f"copy={b.name},{b.n},{b.ok}")       // copy=alice,42,true
    @print(f"src={a.name}")                     // src=alice

    // ① writable in-place reset
    Rec r = Rec { name: "x", n: 9, ok: true }
    blank(&!r)
    @print(f"blank=[{r.name}],{r.n},{r.ok}")    // blank=[],0,false

    // ② enum variant metadata
    @print(variant_info(Shape)())               // Circle#0(int) Square#1(int) Empty#2()

    // ③ write-once deserialize via f.type
    Conf cfg = Conf { port: 8080, tls: true, host: "h" }
    Value tree = cfg.to_value()
    Conf back = from_value_generic(Conf)(tree)
    @print(f"deser={back.port},{back.tls},{back.host}")  // deser=8080,true,h

    @print("COMPTIME V2 DONE")
    return 0
}
