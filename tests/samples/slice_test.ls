// Borrowed slices `&[T]` (docs/plan_borrow_extension.md): a {ptr,len} view over
// a contiguous Vec(T) range, created by `v[a..b]`. Non-owning, bounds-checked,
// governed by the borrow escape analysis. Covers creation, index, len(), for-in,
// slice-as-parameter (zero-copy), sub-slicing, and has_drop (Str) elements.
// Marker driver: prints "SL PASS" lines, never "FAIL"; memcheck 0/0/0.
import std.vec
import std.str

fn sum(&[int] s) -> int {
    int t = 0
    for x in s { t = t + x }
    return t
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

    // has_drop (Str) elements: index read deep-clones (owned + dropped); for-in
    // reads in place. memcheck proves no leak / double-free.
    Vec(Str) names = ["alice", "bob", "carol", "dave"]
    Str picked = names[1..3][0]
    if (picked.len() == 3) { print("SL PASS strclone") } else { print("SL FAIL strclone") }
    int total = 0
    for n in names[1..4] { total = total + n.len() }
    if (total == 12) { print("SL PASS strforin") } else { print("SL FAIL strforin") }

    print("SL PASS")
    return 0
}
