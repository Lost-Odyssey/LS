// Regression: map.keys() / map.values() iteration + bound vec result.
// Previously `for k in m.keys()` iterated zero times (silent), and
// `vec(K) ks = m.keys()` errored "unknown map method 'keys'".
// Self-verifying: prints "MK PASS" only if every check holds.

fn check(bool cond, int id) -> bool {
    if !cond { print(id); print("MK FAIL") }
    return cond
}

struct P { string name; int age }

fn main() {
    bool ok = true

    // --- string keys: count + get(k) round-trip ---
    map(string, string) a = {}
    a.set("class", "box")
    a.set("id", "x")
    int c = 0
    for k in a.keys() { c = c + 1 }
    if !check(c == 2, 1) { ok = false }

    int kv = 0
    for k in a.keys() { kv = kv + a.get(k).length }   // "box"=3 + "x"=1 = 4
    if !check(kv == 4, 2) { ok = false }

    // --- keys() as a bound vec expression ---
    vec(string) ks = a.keys()
    if !check(ks.length == 2, 3) { ok = false }

    // --- int keys + values sums (order-independent) ---
    map(int, int) m = {}
    m.set(10, 100)
    m.set(20, 200)
    m.set(30, 300)
    int sk = 0
    int sv = 0
    for k in m.keys() { sk = sk + k }
    for v in m.values() { sv = sv + v }
    if !check(sk == 60, 4) { ok = false }
    if !check(sv == 600, 5) { ok = false }

    // --- break / continue funnel through the same exit (no leak/double-free) ---
    int seen = 0
    for k in m.keys() { seen = seen + 1; if seen == 2 { break } }
    if !check(seen == 2, 6) { ok = false }
    int cont = 0
    for k in m.keys() { if k == 20 { continue } cont = cont + k }   // 10 + 30
    if !check(cont == 40, 7) { ok = false }

    // --- empty map: zero iterations ---
    map(string, int) e = {}
    int ec = 0
    for k in e.keys() { ec = ec + 1 }
    if !check(ec == 0, 8) { ok = false }

    // --- values() of has_drop struct: deep-cloned, dropped cleanly ---
    map(string, P) ps = {}
    ps.set("a", P { name: "alice", age: 30 })
    ps.set("b", P { name: "bob", age: 25 })
    int tot = 0
    for v in ps.values() { tot = tot + v.age }
    if !check(tot == 55, 9) { ok = false }

    if ok { print("MK PASS") }
}
