// A-3 (docs/bugs_deferred_p5_4.md §3): a struct field that references a struct
// type defined later (forward reference) is not supported and must produce a
// clean "unknown type" error and exit gracefully — previously it segfaulted
// (deref of NULL field type in the has_drop scan).

struct Outer {
    Inner inner;
}
struct Inner {
    Str data;
}
def main() {
    Outer o
    o.inner.data = "test"
    @print(o.inner.data)
}
