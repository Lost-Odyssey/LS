// vec Batch C end-to-end test: extend(), insert()
import std.vec
import std.str

fn main() -> int {
    // ===================== extend() =====================

    // extend int vec
    Vec(int) a = {}
    a.push(1)
    a.push(2)
    Vec(int) b = {}
    b.push(3)
    b.push(4)
    b.push(5)
    a.extend(b)
    print(a.len())   // 5
    print(a[0])      // 1
    print(a[1])      // 2
    print(a[2])      // 3
    print(a[3])      // 4
    print(a[4])      // 5

    // extend with empty src — nop
    Vec(int) c = {}
    a.extend(c)
    print(a.len())   // 5 (unchanged)

    // extend empty self with non-empty src
    Vec(int) d = {}
    Vec(int) e = {}
    e.push(10)
    e.push(20)
    d.extend(e)
    print(d.len())   // 2
    print(d[0])      // 10
    print(d[1])      // 20

    // extend string vec — elements must be deep-cloned (independent copies)
    Vec(Str) sa = {}
    sa.push("hello")
    sa.push("world")
    Vec(Str) sb = {}
    sb.push("foo")
    sb.push("bar")
    sa.extend(sb)
    print(sa.len())   // 4
    print(sa[0])      // hello
    print(sa[1])      // world
    print(sa[2])      // foo
    print(sa[3])      // bar

    // extend self with itself-style (separate vec, same values)
    Vec(int) f = {}
    f.push(7)
    f.push(8)
    Vec(int) g = {}
    g.push(7)
    g.push(8)
    f.extend(g)
    print(f.len())   // 4
    print(f[0])      // 7
    print(f[1])      // 8
    print(f[2])      // 7
    print(f[3])      // 8

    // ===================== insert() =====================

    // insert at beginning
    Vec(int) v = {}
    v.push(2)
    v.push(3)
    v.push(4)
    v.insert(0, 1)
    print(v.len())   // 4
    print(v[0])      // 1
    print(v[1])      // 2
    print(v[2])      // 3
    print(v[3])      // 4

    // insert in middle
    v.insert(2, 99)
    print(v.len())   // 5
    print(v[0])      // 1
    print(v[1])      // 2
    print(v[2])      // 99
    print(v[3])      // 3
    print(v[4])      // 4

    // insert at end (same as push)
    v.insert(5, 100)
    print(v.len())   // 6
    print(v[5])      // 100

    // insert into empty vec at index 0
    Vec(int) empty = {}
    empty.insert(0, 42)
    print(empty.len())    // 1
    print(empty[0])       // 42

    // insert on string vec — ownership transfer (no double-free)
    Vec(Str) sv = {}
    sv.push("first")
    sv.push("third")
    sv.insert(1, "second")
    print(sv.len())   // 3
    print(sv[0])      // first
    print(sv[1])      // second
    print(sv[2])      // third

    // insert at beginning of string vec
    sv.insert(0, "zero")
    print(sv.len())   // 4
    print(sv[0])      // zero
    print(sv[1])      // first

    return 0
}
