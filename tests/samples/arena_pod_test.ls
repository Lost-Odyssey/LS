// arena_pod_test.ls — std.mem.arena Phase 1: typed POD bump arena.
// Covers: alloc/get!/get/set!, auto-grow (realloc past initial cap), stable
// index handles across growth, struct elements (padded stride), handle-linked
// list build+traverse, reset()+reuse (buffer kept), empty?, bounds-checked get.
// All buffers freed by Arena.__drop → memcheck must be 0/0/0.
// Prints "ok <label>" / "FAIL <label>" then "ARENA POD PASS".

import std.mem.arena
import std.core.str

struct Node { i64 val; int next }   // POD: i64 + index handle, no has_drop field
struct Pt { int x; int y }          // another POD type (Region heterogeneity)

def check(bool c, Str l) {
    if c { @print(f"ok {l}") } else { @print(f"FAIL {l}") }
}

def main() -> int {
    // ---- basic alloc / get! / get / set! ----
    Arena(int) a = {}
    check(a.empty?(), "fresh arena is empty")
    int h0 = a.alloc(10)
    int h1 = a.alloc(20)
    int h2 = a.alloc(30)
    check(a.len() == 3, "len after 3 allocs = 3")
    check(h0 == 0 && h1 == 1 && h2 == 2, "handles are 0,1,2")
    check(a.get!(h0) == 10 && a.get!(h2) == 30, "get! reads back values")
    a.set!(h1, 99)
    check(a.get!(h1) == 99, "set! mutates by handle")

    // ---- safe bounds-checked get ----
    check(a.get(1).unwrap_or(-1) == 99, "get(1) in range -> Some(99)")
    check(a.get(50).is_none?(), "get(50) out of range -> None")
    check(a.get(0 - 1).is_none?(), "get(-1) -> None")

    // ---- auto-grow: alloc well past initial cap, handles stay valid ----
    Arena(int) g = {}
    int n = 1000
    for (int i = 0; i < n; i = i + 1) {
        int h = g.alloc(i * 3)
        if h != i { check(false, "handle == alloc order during growth") }
    }
    check(g.len() == n, "len == 1000 after growth")
    check(g.cap() >= n, "cap grew to hold 1000")
    i64 gsum = 0
    for (int i = 0; i < n; i = i + 1) { gsum = gsum + g.get!(i) as i64 }
    check(gsum == 1498500, "sum of 3*0..3*999 survives realloc")   // 3*(999*1000/2)

    // ---- struct elements + handle-linked list ----
    Arena(Node) la = {}
    int head = 0 - 1
    for (int i = 0; i < 5; i = i + 1) {
        head = la.alloc(Node { val: i as i64, next: head })
    }
    i64 lsum = 0
    int cur = head
    int hops = 0
    while cur >= 0 {
        Node node = la.get!(cur)
        lsum = lsum + node.val
        cur = node.next
        hops = hops + 1
    }
    check(lsum == 10, "linked-list sum 0..4 = 10")
    check(hops == 5, "traversed exactly 5 nodes")

    // ---- reset + reuse: buffer kept, handles restart ----
    int cap_before = g.cap()
    g.reset()
    check(g.len() == 0, "reset -> len 0")
    check(g.cap() == cap_before, "reset keeps the buffer (cap unchanged)")
    int r0 = g.alloc(777)
    check(r0 == 0, "reused handle restarts at 0")
    check(g.get!(r0) == 777, "reused slot holds new value")

    // ---- Region: heterogeneous byte arena, raw *T slices from one block ----
    Region rg = {}
    rg.reserve(65536)
    check(rg.used() == 0, "fresh region used 0")
    *Node rn0 = rg.alloc_bytes(sizeof(Node)) as *Node
    rn0[0] = Node { val: 11 as i64, next: 0 - 1 }
    Pt rp = Pt { x: 3, y: 4 }                    // a different POD type from the SAME block
    *Pt rpp = rg.alloc_bytes(sizeof(Pt)) as *Pt
    rpp[0] = rp
    *Node rn1 = rg.alloc_bytes(sizeof(Node)) as *Node
    rn1[0] = Node { val: 22 as i64, next: 0 - 1 }
    check(rn0[0].val == 11 && rn1[0].val == 22, "region heterogeneous Node values")
    check(rpp[0].x == 3 && rpp[0].y == 4, "region interleaved Pt value")
    i64 used_before = rg.used()
    check(used_before > 0, "region used advanced")
    rg.reset()
    check(rg.used() == 0, "region reset rewinds used to 0")
    check(rg.cap() == 65536, "region reset keeps block (cap unchanged)")
    *Node rn2 = rg.alloc_bytes(sizeof(Node)) as *Node
    rn2[0] = Node { val: 333 as i64, next: 0 - 1 }
    check(rn2[0].val == 333, "region reused slot")

    // ---- Region.str: interned has_drop Str in a region (cap == -2 sentinel) ----
    Region sr = {}
    sr.reserve(4096)
    Str s_a = sr.str("hello")
    Str s_b = sr.str("a_longer_interned_string")
    check(s_a.eq?("hello"), "arena str value (short)")
    check(s_b.len() == 24, "arena str len (long)")
    check(sr.used() > 0, "arena str bytes live in region")
    // clone PROMOTES off the region (deep copy to malloc) -> survives reset
    Str s_keep = s_a.copy()
    // mutation PROMOTES too (reserve copy-on-grow on cap <= 0)
    Str s_mut = sr.str("abc")
    s_mut.push_str("def")
    check(s_mut.eq?("abcdef"), "arena str mutation auto-promotes")
    sr.reset()
    check(sr.used() == 0, "region of strings reset")
    // s_a / s_b now dangle (contract: not read post-reset); the clone is independent:
    check(s_keep.eq?("hello"), "cloned arena str survives reset (promoted)")

    @print("ARENA POD PASS")
    return 0
}
