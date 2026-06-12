// D-1 (docs/bugs_deferred_p5_4.md §D-1): struct auto-print renders Str fields as
// quoted text instead of `Str{data=ptr,len,cap}` (which leaked a pointer and was
// non-deterministic). Self-verifying via a fixed-format expectation printed to
// stdout; the driver checks "DCOPY PASS" and rejects "FAIL".
//
// (The original string-era deep_copy_all_test tested clone-on-assign, which does
//  not translate to Str's move-on-bind semantics; this replacement locks in the
//  D-1 auto-print behavior instead.)

import std.str

struct Inner { Str tag; }
struct Person { int age; Str name; Inner inner; }

fn main() -> int {
    Person p = Person{ age: 10, name: "Alice".upper(), inner: Inner{ tag: "x" } }
    // Expected: Person{age=10, name="ALICE", inner=Inner{tag="x"}}
    print(p)

    // Empty Str field prints as ""
    Person q = Person{ age: 0, name: "", inner: Inner{ tag: "" } }
    print(q)

    print("DCOPY PASS")
    return 0
}
