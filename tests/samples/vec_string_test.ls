// vec_string_test.ls — Vec(string) memory safety test
// Verifies: push dynamic strings, index write (frees old), pop (frees),
//           clear (frees all), scope exit cleanup, chained methods

import std.vec

fn collect_words() -> int {
    Vec(string) v = {}
    v.push("hello".upper())    // HELLO — heap
    v.push("world".upper())    // WORLD — heap
    v.push("foo")              // static

    // pop removes "foo" (static — no free needed); discarded rvalue (F2)
    v.pop()

    // pop removes "WORLD" (heap — freed); discarded rvalue Option(string) must
    // drop its inner string (VR-LIM-014 / F2)
    v.pop()

    // only "HELLO" remains
    if (v.len() != 1) { return -1 }
    if (v[0] != "HELLO") { return -2 }

    // overwrite index 0: frees "HELLO", stores new heap string
    v[0] = "new".upper()      // NEW — heap
    if (v[0] != "NEW") { return -3 }

    return 1
    // scope exit: "NEW" freed
}

fn clear_test() -> int {
    Vec(string) v = {}
    v.push("alpha".upper())   // ALPHA — heap
    v.push("beta".upper())    // BETA  — heap
    v.clear()                 // frees ALPHA and BETA; len = 0

    if (v.len() != 0) { return -1 }

    v.push("x")               // static
    v.push("y".upper())       // Y — heap
    if (v.len() != 2) { return -2 }
    if (v[0] != "x") { return -3 }
    if (v[1] != "Y") { return -4 }
    return 1
    // scope exit: "Y" freed, "x" static
}

fn for_in_test() -> int {
    Vec(string) v = {}
    v.push("ab")
    v.push("cde")
    v.push("f")

    int total = 0
    for s in v {
        total = total + s.length
    }
    // 2 + 3 + 1 = 6
    if (total != 6) { return -1 }
    return 1
}

fn chained_push_test() -> int {
    Vec(string) v = {}
    string base = "hello"
    v.push(base.upper())           // HELLO
    v.push(base.upper().lower())   // hello (two temporaries, both freed)
    v.push(base + " world")        // hello world

    if (v.len() != 3) { return -1 }
    if (v[0] != "HELLO") { return -2 }
    if (v[1] != "hello") { return -3 }
    if (v[2] != "hello world") { return -4 }
    return 1
}

fn main() -> int {
    int r1 = collect_words()
    print(r1)          // 1

    int r2 = clear_test()
    print(r2)          // 1

    int r3 = for_in_test()
    print(r3)          // 1

    int r4 = chained_push_test()
    print(r4)          // 1

    return 0
}
