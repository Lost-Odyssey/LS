// Phase 2 (borrow extension): a free function with one borrow parameter may
// return a borrow derived from THAT parameter — but returning a borrow of a
// LOCAL must still be rejected (it would dangle once the function returns).
struct Foo { int x }

fn bad(&Foo a) -> &Foo {
    Foo local = Foo{x: 9}
    return &local            // borrow of a local → must reject (dangling)
}

fn main() -> int {
    Foo f = Foo{x: 1}
    print(bad(&f).x)
    return 0
}
