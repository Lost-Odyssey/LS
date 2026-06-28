// Regression: generic free-function call ergonomics (independent of comptime).
//   Gap 1 — value-arg type inference: `f(v)` infers the type param from the arg,
//           no explicit `f(Type)(v)` needed.
//   Gap 2 — `&T` borrow params: auto-borrow a value (`f(v)`) and accept an
//           explicit `&v`, with zero-copy reads (memcheck 0/0/0).
// Uses comptime only as a convenient way to write a body over T's fields; the
// inference/borrow machinery being tested is comptime-independent.

import std.core.str

struct Point { int x; int y }
struct Rect  { int w; int h }
struct Named { Str tag; int n }

// Gap 2: read-only `&T` borrow param — sums the int fields without copying v.
def sum_ints(T)(&T v) -> int {
    int t = 0
    comptime for f in fields(T) {
        comptime if f.type_name == "int" { t = t + v.(f) }
    }
    return t
}

// Gap 2: read a Str field THROUGH the borrow (memcheck-sensitive — the field is
// cloned out, the borrow source keeps ownership, so no leak / no double-free).
def first_str(T)(&T v) -> Str {
    Str out = "?"
    comptime for f in fields(T) {
        comptime if f.type_name == "Str" { out = v.(f) }
    }
    return out
}

// Gap 1: by-value `T` param (non-ref) still works with inferred type args.
def sum_byval(T)(T v) -> int {
    int t = 0
    comptime for f in fields(T) {
        comptime if f.type_name == "int" { t = t + v.(f) }
    }
    return t
}

def main() -> int {
    Point p  = Point { x: 10, y: 20 }
    Rect  r  = Rect  { w: 3,  h: 4 }
    Named nm = Named { tag: "hi", n: 99 }

    // Gap 1+2: inferred type args + auto-borrow over 2+ distinct struct types.
    @print(sum_ints(p))        // 30
    @print(sum_ints(r))        // 7
    @print(sum_ints(nm))       // 99

    // The borrow does not move/consume the source — still usable afterwards.
    @print(p.x)                // 10
    @print(nm.n)               // 99

    // Explicit `&x` argument to a `&T` param (with inference).
    @print(sum_ints(&r))       // 7

    // Explicit type args must still work.
    @print(sum_ints(Point)(p)) // 30

    // Read an owned Str field through the borrow (clone-out, source kept).
    Str s = first_str(nm)
    @print(s)                  // hi
    @print(nm.tag)             // hi  (source's Str field intact after the borrow)

    // Gap 1: by-value T (non-ref) param, inferred.
    @print(sum_byval(p))       // 30

    @print("GFFB DONE")
    return 0
}
