// P7-mig: impl Hash for Str + operator == (trait Equal) => Str usable as Map key.

import std.core.str
import std.core.hash
import std.core.map

def main() {
    // -- Hash: deterministic, differs across values, equal across equal values
    Str a = "alpha"
    Str b = "beta"
    Str a2 = "alpha"
    if a.hash() != a2.hash() { @print("FAIL: equal Str hash mismatch") return }
    if a.hash() == b.hash() { @print("FAIL: alpha/beta hash collide") return }
    @print("PASS: Str hash")

    // -- Equal operator (and derived !=)
    if a == b { @print("FAIL: alpha == beta") return }
    if a != a2 { @print("FAIL: alpha != alpha") return }
    @print("PASS: Str ==/!=")

    // -- Str as Map key (where K: Hash + Equal), enough inserts to force a grow
    Map(Str, int) m = {}
    m.set("one", 1)
    m.set("two", 2)
    m.set("three", 3)
    for (int i = 0; i < 50; i = i + 1) {
        Str k = f"key{i}"
        m.set(k, i * 10)
    }
    if m.len() != 53 { @print("FAIL: map len") @print(m.len()) return }
    int v = m.get("two").unwrap_or(-1)
    if v != 2 { @print("FAIL: get two") return }
    Str probe = "key37"
    int v2 = m.get(probe).unwrap_or(-1)
    if v2 != 370 { @print("FAIL: get key37") return }
    if m.has?("nope") { @print("FAIL: phantom key") return }
    @print("PASS: Map(Str,int) set/get/grow")

    // -- overwrite + remove with has_drop Str keys
    m.set("two", 22)
    int v3 = m.get("two").unwrap_or(-1)
    if v3 != 22 { @print("FAIL: overwrite") return }
    int r = m.remove("one").unwrap_or(-1)
    if r != 1 { @print("FAIL: remove") return }
    if m.len() != 52 { @print("FAIL: len after remove") return }
    @print("PASS: overwrite/remove")

    @print("STRHK PASS")
}
