// A-1 (docs/bugs_deferred_p5_4.md §1): nested def/struct/impl definitions inside
// a function body are not supported and must be rejected at parse time with a
// clear error (previously crashed deep in codegen with "no terminator").

def outer() -> int {
    def inner() -> int {
        return 42
    }
    return inner()
}

def main() -> int {
    @print(outer())
    return 0
}
