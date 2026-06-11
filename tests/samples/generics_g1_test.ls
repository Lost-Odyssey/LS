// Phase G1: user-defined generic struct tests

import std.str

struct Pair(T, U) {
    T first
    U second
}

impl(T, U) Pair(T, U) {
    fn get_first() -> T { return self.first }
    fn get_second() -> U { return self.second }
}

fn main() {
    // G1.1: POD fields, has_drop=false
    Pair(int, int) p1 = Pair(int, int) { first: 1, second: 2 }
    print(p1.first)
    print(p1.second)

    // G1.2: string field, has_drop=true
    Pair(Str, int) p2 = Pair(Str, int) { first: "hello", second: 42 }
    print(p2.first)
    print(p2.second)

    // G1.3: different instantiation of same template
    Pair(int, Str) p3 = Pair(int, Str) { first: 99, second: "world" }
    print(p3.first)
    print(p3.second)

    // G1.4: nested generics
    Pair(Pair(int, int), Str) p4 = Pair(Pair(int, int), Str) {
        first: p1,
        second: "nested"
    }
    print(p4.first.first)
    print(p4.second)

    // G1.5: generic method calls
    print(p1.get_first())
    print(p1.get_second())
    print(p2.get_first())
    print(p2.get_second())
    print(p3.get_first())
    print(p3.get_second())
}
