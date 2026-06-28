// rawvec_sizeof_test.ls — Step 2: sizeof(T) real compile-time evaluation.
// Covers: primitive sizes, pointer size, and sizeof(T) inside a generic struct
// method (the actual RawVec pattern) monomorphizing to the right size per
// instantiation, plus arithmetic (count * sizeof(T)).
// Prints "ok <label>" / "FAIL <label>" then "SIZEOF PASS".

import std.core.str

def check(bool c, Str l) {
    if c { @print(f"ok {l}") } else { @print(f"FAIL {l}") }
}

// Generic struct exercising sizeof(T) in a method body — RawVec's usage pattern.
struct Box(T) { T val }

def new_box(T)(T v) -> Box(T) { return Box(T) { val: v } }

methods(T) Box(T) {
    def elem_size(&self) -> i64 { return sizeof(T) }
    def bytes_for(&self, int n) -> i64 { return n * sizeof(T) }
}

def main() {
    // ---- primitive sizes (compile-time constants) ----
    check(sizeof(int)  == 4, "sizeof int = 4")
    check(sizeof(i64)  == 8, "sizeof i64 = 8")
    check(sizeof(i32)  == 4, "sizeof i32 = 4")
    check(sizeof(i16)  == 2, "sizeof i16 = 2")
    check(sizeof(i8)   == 1, "sizeof i8 = 1")
    check(sizeof(u8)   == 1, "sizeof u8 = 1")
    check(sizeof(bool) == 1, "sizeof bool = 1")
    check(sizeof(f64)  == 8, "sizeof f64 = 8")
    check(sizeof(f32)  == 4, "sizeof f32 = 4")

    // ---- pointer size (x64) ----
    check(sizeof(*int) == 8, "sizeof *int = 8")
    check(sizeof(*u8)  == 8, "sizeof *u8 = 8")

    // ---- sizeof(T) inside generic method, monomorphized per instantiation ----
    Box(int) bi = new_box(int)(7)
    Box(f64) bf = new_box(f64)(1.5)
    Box(u8)  bu = new_box(u8)(0 as u8)
    check(bi.elem_size() == 4, "Box(int).elem_size = 4")
    check(bf.elem_size() == 8, "Box(f64).elem_size = 8")
    check(bu.elem_size() == 1, "Box(u8).elem_size = 1")

    // ---- arithmetic with sizeof (RawVec buffer-byte computation) ----
    check(bi.bytes_for(10) == 40, "10 * sizeof(int) = 40")
    check(bf.bytes_for(10) == 80, "10 * sizeof(f64) = 80")
    check(bu.bytes_for(64) == 64, "64 * sizeof(u8) = 64")

    @print("SIZEOF PASS")
}
