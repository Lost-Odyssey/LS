// Phase 1 (borrow extension, docs/plan_borrow_extension.md §3): named non-escaping
// local borrows `&T r = &x` / `&!T m = &!x` / `&T r2 = r1`. Covers read-only
// borrow reads, writable-borrow mutation propagating to the referent, re-borrow,
// has_drop (Str) referent, and the referent staying usable after the borrow.
// Marker driver: prints "LB PASS" lines, never "FAIL"; memcheck 0/0/0.
import std.core.str

struct P { int x; int y }
struct H { Str name }

def check_read() {
    P p = P{x: 3, y: 7}
    &P r = &p
    if (r.x == 3 && r.y == 7) { @print("LB PASS read") } else { @print("LB FAIL read") }
}

def check_write() {
    P p = P{x: 1, y: 2}
    &!P m = &!p
    m.x = 99
    m.y = 88
    // Mutation through the writable borrow must be visible on the referent.
    if (p.x == 99 && p.y == 88) { @print("LB PASS write") } else { @print("LB FAIL write") }
}

def check_reborrow() {
    P p = P{x: 5, y: 6}
    &P r = &p
    &P r2 = r
    if (r2.x == 5 && r2.y == 6) { @print("LB PASS reborrow") } else { @print("LB FAIL reborrow") }
}

def check_hasdrop() {
    H h = H{name: "hello"}
    &H r = &h
    if (r.name.len() == 5) { @print("LB PASS hasdrop") } else { @print("LB FAIL hasdrop") }
    // The referent remains usable after the borrow is read.
    if (h.name.len() == 5) { @print("LB PASS srcalive") } else { @print("LB FAIL srcalive") }
}

def main() -> int {
    check_read()
    check_write()
    check_reborrow()
    check_hasdrop()
    @print("LB PASS")
    return 0
}
