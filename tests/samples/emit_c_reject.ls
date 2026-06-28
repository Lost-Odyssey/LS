// Out-of-subset for `ls emit-c`: uses Str (a heap container). emit-c must
// reject this with a clear diagnostic and write no output file.
import std.core.str

def kernel(*f32 p) -> f32 {
    Simd(f32, 16) v = __simd_load(p, 0)
    return __simd_reduce_add(v)
}

def greet(Str name) -> Str {
    return name.upper()
}
