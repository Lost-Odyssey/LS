// comptime field iteration (Stage 3b), step 2: read-only unroll.
//   comptime for f in fields(T) { ... f.name / f.index / f.type_name / v.(f) ... }
// Generic free functions take T by read-only borrow `&T` and are called with
// inferred type args (`to_csv(p)`); explicit type args still work (`to_csv(Point)(..)`
// in dump_line). Borrow = zero-copy, so memcheck stays 0/0/0. JIT+AOT+memcheck.

struct Point { int x; int y }
struct User { Str name; int age; bool active }
struct Line { Point a; Point b }

// write-once: one definition serializes any struct to a CSV row (zero-copy &T)
def to_csv(T)(&T v) -> Str {
    Str out = ""
    comptime for f in fields(T) {
        if f.index > 0 { out = out + "," }
        out = out + f"{v.(f)}"
    }
    return out
}

// field metadata: name + declared type name (primitives stay clean)
def describe(T)(&T v) -> Str {
    Str out = ""
    comptime for f in fields(T) {
        out = out + f.name + ":" + f.type_name + ";"
    }
    return out
}

// comptime if dispatches on field type: `if v.(f)` is only valid for bool fields;
// for Str/int fields that branch is dropped BEFORE checking (no type error).
def count_flags(T)(&T v) -> int {
    int n = 0
    comptime for f in fields(T) {
        comptime if f.type_name == "bool" {
            if v.(f) { n = n + 1 }
        }
    }
    return n
}

// else-chain: bool -> flag, int -> num, anything else -> other
def kinds(T)(&T v) -> Str {
    Str out = ""
    comptime for f in fields(T) {
        comptime if f.type_name == "bool" {
            out = out + f.name + "=flag "
        } else comptime if f.type_name == "int" {
            out = out + f.name + "=num "
        } else {
            out = out + f.name + "=other "
        }
    }
    return out
}

// composition: a comptime-for body feeds an aggregate field v.(f) into another
// generic comptime function. Uses an EXPLICIT type arg `to_csv(Point)(..)` to
// prove explicit instantiation still works (and to auto-borrow the rvalue field).
def dump_line(Line v) -> Str {
    Str out = ""
    comptime for f in fields(Line) {
        out = out + f.name + "=[" + to_csv(Point)(v.(f)) + "] "
    }
    return out
}

def main() -> int {
    Point p = Point { x: 3, y: 7 }

    // non-generic comptime for, expanded directly in a function body
    Str manual = ""
    comptime for f in fields(Point) {
        manual = manual + f.name + "=" + f"{p.(f)}" + " "
    }
    @print(manual)                      // x=3 y=7

    // generic instantiation with INFERRED type args (zero-copy &T borrow)
    @print(to_csv(p))                   // 3,7
    @print(p.x)                         // 3  (source still usable after borrow)

    User u = User { name: "ann", age: 30, active: true }
    @print(to_csv(u))                   // ann,30,true  (v.(f) reads a Str field too)

    @print(describe(p))                 // x:int;y:int;

    // comptime if: type-directed branch pruning (inferred type args)
    @print(f"flags={count_flags(u)}")   // flags=1 (only active true; name/age branches dropped)
    @print(kinds(u))                    // name=other age=num active=flag

    // composition: nested struct, aggregate v.(f) into an explicit-type-arg call
    Line ln = Line { a: Point { x: 1, y: 2 }, b: Point { x: 3, y: 4 } }
    @print(dump_line(ln))               // a=[1,2] b=[3,4]

    @print("COMPTIME STEP2 DONE")
    return 0
}
