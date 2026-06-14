// Borrowed slices `&array(T)` (docs/plan_borrow_extension.md): a {ptr,len} view over
// a contiguous Vec(T) range, created by `v[a..b]`. Non-owning, bounds-checked,
// governed by the borrow escape analysis. Covers creation, index, len(), for-in,
// slice-as-parameter (zero-copy), sub-slicing, and has_drop (Str) elements.
// Marker driver: prints "SL PASS" lines, never "FAIL"; memcheck 0/0/0.
import std.vec
import std.str

fn sum(&array(int) s) -> int {
    int t = 0
    for x in s { t = t + x }
    return t
}

// Writable slice parameter: double each element of the borrowed range in place.
fn double_all(&!array(int) s) {
    int i = 0
    for x in s { s[i] = s[i] * 2  i = i + 1 }
}

struct Buf { Vec(int) data }
impl Buf {
    // Zero-copy view API: return a borrowed window into self's buffer (single-
    // input lifetime elision — the view borrows self).
    fn window(&self, int a, int b) -> &array(int) { return self.data[a..b] }
}

fn main() -> int {
    Vec(int) v = [10, 20, 30, 40, 50]

    // Creation + bounds-checked element read.
    if (v[1..4][0] == 20 && v[1..4][2] == 40) { print("SL PASS index") } else { print("SL FAIL index") }

    // Length of the view.
    if (v[1..4].len() == 3) { print("SL PASS len") } else { print("SL FAIL len") }

    // Iterate a sub-range directly (no copy).
    int s1 = 0
    for x in v[0..3] { s1 = s1 + x }
    if (s1 == 60) { print("SL PASS forin") } else { print("SL FAIL forin") }

    // Pass a slice to a reusable function — zero-copy over any sub-range.
    if (sum(v[1..5]) == 140 && sum(v[0..5]) == 150) { print("SL PASS param") } else { print("SL FAIL param") }

    // Sub-slice of a slice.
    if (v[1..5][1..3].len() == 2 && v[1..5][1..3][0] == 30) { print("SL PASS subslice") } else { print("SL FAIL subslice") }

    // Bind a slice to a named local (pins the source v while it is alive).
    &array(int) sl = v[1..4]
    int s2 = 0
    for x in sl { s2 = s2 + x }
    if (sl.len() == 3 && sl[0] == 20 && s2 == 90) { print("SL PASS bind") } else { print("SL FAIL bind") }

    // Slice-returning method (zero-copy window), used immediately and bound.
    Buf bf = Buf{data: [1, 2, 3, 4, 5, 6]}
    if (bf.window(2, 5).len() == 3 && bf.window(2, 5)[0] == 3) { print("SL PASS retimm") } else { print("SL FAIL retimm") }
    &array(int) w = bf.window(0, 4)
    int s3 = 0
    for x in w { s3 = s3 + x }
    if (s3 == 10) { print("SL PASS retbind") } else { print("SL FAIL retbind") }

    // Writable slice: mutate a sub-range in place through `&!array(T)`.
    Vec(int) m = [1, 2, 3, 4, 5]
    &!array(int) ms = m[1..4]
    ms[0] = 99
    ms[2] = 77
    if (m[1] == 99 && m[3] == 77 && m[0] == 1) { print("SL PASS mutstore") } else { print("SL FAIL mutstore") }
    double_all(ms)
    if (m[1] == 198 && m[2] == 6 && m[3] == 154) { print("SL PASS mutparam") } else { print("SL FAIL mutparam") }

    // has_drop (Str) elements: index read deep-clones (owned + dropped); for-in
    // reads in place. memcheck proves no leak / double-free.
    Vec(Str) names = ["alice", "bob", "carol", "dave"]
    Str picked = names[1..3][0]
    if (picked.len() == 3) { print("SL PASS strclone") } else { print("SL FAIL strclone") }
    int total = 0
    for n in names[1..4] { total = total + n.len() }
    if (total == 12) { print("SL PASS strforin") } else { print("SL FAIL strforin") }

    // Open ranges: v[a..] (to end), v[..b] (from start), v[..] (whole).
    Vec(int) r = [10, 20, 30, 40, 50]
    if (r[2..].len() == 3 && r[2..][0] == 30) { print("SL PASS openhi") } else { print("SL FAIL openhi") }
    if (r[..3].len() == 3 && r[..3][2] == 30) { print("SL PASS openlo") } else { print("SL FAIL openlo") }
    if (r[..].len() == 5) { print("SL PASS openfull") } else { print("SL FAIL openfull") }

    // Direct `v[a..b]` passed to a writable-slice parameter (no intermediate bind).
    Vec(int) d = [1, 2, 3, 4]
    double_all(d[0..4])
    if (d[0] == 2 && d[3] == 8) { print("SL PASS directmut") } else { print("SL FAIL directmut") }

    // Str byte slice: a `&array(u8)` view over the string's bytes.
    Str txt = "hello"
    int bsum = 0
    for b in txt[1..4] { bsum = bsum + (b as int) }   // 'e'+'l'+'l' = 317
    if (txt[1..4].len() == 3 && bsum == 317 && (txt[2..][0] as int) == 108) { print("SL PASS strbytes") } else { print("SL FAIL strbytes") }

    // has_drop (Str) writable slice: store drops the old element, moves the new in.
    Vec(Str) ws = ["alice", "bob", "carol", "dave"]
    &!array(Str) wss = ws[1..3]
    wss[0] = "BOByes"
    wss[1] = "X"
    if (ws[1].len() == 6 && ws[2].len() == 1 && ws[0].len() == 5 && ws[3].len() == 4) { print("SL PASS hdmut") } else { print("SL FAIL hdmut") }

    print("SL PASS")
    return 0
}
