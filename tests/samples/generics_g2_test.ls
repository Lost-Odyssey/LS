// Phase G2: user-defined generic function tests

import std.core.str

def identity(T)(T x) -> T {
    return x
}

def add_pair(T)(T a, T b) -> T {
    return a + b
}

struct Pair(T, U) {
    T first
    U second
}

def make_pair(T, U)(T a, U b) -> Pair(T, U) {
    return Pair(T, U) { first: a, second: b }
}

def main() {
    // G2.1: identity with int
    int a = identity(int)(42)
    @print(a)

    // G2.2: identity with string
    Str s = identity(Str)("hello")
    @print(s)

    // G2.3: identity with f64
    f64 f = identity(f64)(3.14)
    @print(f)

    // G2.4: identity with bool
    bool b = identity(bool)(true)
    @print(b)

    // G2.5: two-param same type
    int sum = add_pair(int)(10, 20)
    @print(sum)

    // G2.6: multi-type-param function returning generic struct
    Str w = "world"
    Pair(int, Str) p = make_pair(int, Str)(99, w)
    @print(p.first)
    @print(p.second)

    // G2.7: reuse same instantiation
    int c = identity(int)(7)
    @print(c)
}
