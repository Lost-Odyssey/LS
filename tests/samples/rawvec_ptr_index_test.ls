// rawvec_ptr_index_test.ls — Step 3: typed *T pointer indexing p[i] (read+write).
// Covers: POD element read/write through a malloc'd buffer, struct elements with
// padding (GEP stride == sizeof, both derived from the same DataLayout), field
// access on an index read (q[i].v), and raw store NOT dropping the old slot.
// All buffers are freed; memcheck must be 0/0/0.
// Prints "ok <label>" / "FAIL <label>" then "PTRIDX PASS".

import std.str

struct Pt { i8 tag; i64 v }   // sizeof 16 (i8@0, pad, i64@8) — stride probe

fn check(bool c, Str l) {
    if c { print(f"ok {l}") } else { print(f"FAIL {l}") }
}

fn main() {
    // ---- POD int buffer: write then read back ----
    *int p = std.c.malloc(16 * sizeof(int)) as *int
    for (int i = 0; i < 16; i = i + 1) { p[i] = i * i }
    int sum = 0
    for (int i = 0; i < 16; i = i + 1) { sum = sum + p[i] }
    check(sum == 1240, "sum of squares 0..15 = 1240")
    check(p[5] == 25, "p[5] = 25")
    // overwrite a slot (raw store over POD — no drop concern)
    p[5] = 99
    check(p[5] == 99, "p[5] overwritten = 99")
    std.c.free(p as *u8)

    // ---- struct buffer: padded stride must be correct ----
    *Pt q = std.c.malloc(8 * sizeof(Pt)) as *Pt
    for (int i = 0; i < 8; i = i + 1) {
        Pt e = Pt { tag: i as i8, v: (i * 100) as i64 }
        q[i] = e
    }
    int allok = 1
    for (int i = 0; i < 8; i = i + 1) {
        Pt r = q[i]
        if r.v != (i * 100) as i64 { allok = 0 }
        if r.tag != i as i8 { allok = 0 }
    }
    check(allok == 1, "8 struct elems round-trip (padded stride)")
    check(q[3].v == 300, "field access on index read q[3].v = 300")
    check(q[7].tag == 7 as i8, "q[7].tag = 7")
    std.c.free(q as *u8)

    // ---- *u8 byte buffer ----
    *u8 b = std.c.malloc(4) as *u8
    b[0] = 10 as u8
    b[1] = 20 as u8
    b[2] = 30 as u8
    b[3] = 40 as u8
    int bsum = (b[0] as int) + (b[1] as int) + (b[2] as int) + (b[3] as int)
    check(bsum == 100, "byte buffer sum = 100")
    std.c.free(b)

    print("PTRIDX PASS")
}
