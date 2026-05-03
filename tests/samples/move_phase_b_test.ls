// Phase B move-semantics: if/else branch merging and loop 2-pass.
// This test exercises valid control flow that Phase B must ACCEPT.
// Violations are covered by dedicated .expect_error samples / test_types.c.

fn main() -> int {
    vec(string) v
    v.push("a".upper())
    v.push("b".upper())

    // --- if with else, both branches move → variable is MOVED afterwards.
    //     We simply don't use `m1` afterwards, so this must compile. ---
    string m1 = "alpha".upper()
    bool c = true
    if c {
        v.push(m1)
    } else {
        v.push(m1)
    }

    // --- if-only (no else): then-branch moves → MAYBE_MOVED afterwards.
    //     `m2` cannot be used/re-assigned afterwards (tested in negative sample). ---
    string m2 = "beta".upper()
    if c {
        v.push(m2)
    }

    // --- Loop body with no move: variable remains LIVE and usable after. ---
    string s = "gamma".upper()
    int i = 0
    while i < 2 {
        print(s)
        i = i + 1
    }
    print(s)

    // --- for-in without move inside: loop variable used read-only. ---
    string label = "item: ".upper()
    vec(int) ns
    ns.push(1)
    ns.push(2)
    for n in ns {
        print(label)
        print(n)
    }
    print(label)

    print(v.is_empty())
    return 0
}
