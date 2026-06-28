// Stage 1 static reflection: @derive(Equal, Hash, Order) synthesizes ==, hash,
// and < field-by-field. Covers POD + Str fields, Map keys (Hash+Equal), and
// lexicographic ordering. (std.core.hash is auto-injected for Hash.)
@derive(Equal, Hash, Order)
struct Point { int x; int y }

@derive(Equal, Hash)
struct Named { Str tag; int n }

import std.core.map

def main() {
    Point a = Point { x: 1, y: 2 }
    Point b = Point { x: 1, y: 2 }
    Point c = Point { x: 9, y: 2 }
    if a == b { @print("eq1 PASS") } else { @print("eq1 FAIL") }
    if a == c { @print("eq2 FAIL") } else { @print("eq2 PASS") }

    // Order: (1,2) < (1,3) lexicographically
    Point d = Point { x: 1, y: 3 }
    if a < d { @print("ord1 PASS") } else { @print("ord1 FAIL") }
    if d < a { @print("ord2 FAIL") } else { @print("ord2 PASS") }

    // Hash + Equal -> usable as Map key
    Map(Point, int) m = {}
    m.set(a, 100)
    m.set(c, 300)
    @print(m.get(b).unwrap_or(0 - 1))   // b == a -> 100
    @print(m.get(c).unwrap_or(0 - 1))   // 300

    // Str field: Equal + Hash
    Named s = Named { tag: "hi", n: 5 }
    Named t = Named { tag: "hi", n: 5 }
    if s == t { @print("eq3 PASS") } else { @print("eq3 FAIL") }
    Map(Named, int) mn = {}
    mn.set(s, 7)
    @print(mn.get(t).unwrap_or(0 - 1))  // t == s -> 7

    @print("DERIVE TRAITS DONE")
}
