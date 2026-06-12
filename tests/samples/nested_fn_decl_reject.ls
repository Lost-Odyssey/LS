// A-1 (docs/bugs_deferred_p5_4.md §1): nested fn/struct/impl definitions inside
// a function body are not supported and must be rejected at parse time with a
// clear error (previously crashed deep in codegen with "no terminator").

fn outer() -> int {
    fn inner() -> int {
        return 42
    }
    return inner()
}

fn main() -> int {
    print(outer())
    return 0
}
