// comptime field iteration — v3 ①: comptime match (active-variant value dispatch).
//   comptime match v { vr(p) => ... } expands, once per enum variant, to a real
//   match v { Variant(p) => ..., ... }:
//     vr.name / vr.index / vr.has_payload / vr.payload_count / vr.type_name are
//     compile-time literals; the binder `p` binds the active variant's first
//     payload (the rest bind `_`). Reuses the mature match check + codegen
//     (drop/move/exhaustiveness) — write-once enum show/serialize/visitor, no
//     @derive, works on enums you don't own. JIT + AOT + memcheck 0/0/0.

import std.core.str

enum Shape { Circle(int); Square(int); Empty }   // POD payloads
enum Node  { Leaf(int); Name(Str); Nil }         // an OWNED (Str) payload
enum Pt    { XY(int, int); Origin }              // a multi-payload variant

// write-once enum show — one definition for ANY enum.
def show_enum(T)(&T v) -> Str {
    comptime match v {
        vr(p) => {
            comptime if vr.has_payload {
                return f"{vr.name}({p})"
            } else {
                return vr.name
            }
        }
    }
}

// no-binder metadata form: active variant name + arity (no payload access).
def kind_of(T)(&T v) -> Str {
    comptime match v {
        vr => {
            return f"{vr.name}/{vr.payload_count}"
        }
    }
}

def main() -> int {
    // POD-payload enum
    @print(show_enum(Circle(5)))      // Circle(5)
    @print(show_enum(Square(9)))      // Square(9)
    @print(show_enum(Empty))          // Empty

    // owned Str payload — drop/move through the generated match (memcheck-sensitive)
    Node nm = Name("hi")
    @print(show_enum(nm))             // Name(hi)
    @print(show_enum(Leaf(7)))        // Leaf(7)
    @print(show_enum(Nil))            // Nil

    // multi-payload: binder binds the FIRST payload; metadata sees the arity
    @print(show_enum(XY(3, 4)))       // XY(3)   (p = first payload)
    @print(kind_of(XY(3, 4)))         // XY/2
    @print(kind_of(Origin))           // Origin/0

    @print("COMPTIME MATCH DONE")
    return 0
}
