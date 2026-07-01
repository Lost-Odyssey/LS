// Owned-Str clone inside a MODULE function (see strmod.ls header).
import strmod
import std.core.str

def main() {
    Map(Str, Str) m = strmod.build(100)
    if m.len() != 100 { @print("FAIL: len") return }
    Str probe = "key42"
    Str v = m.get(probe).unwrap_or("miss")
    if !v.eq?("val42") { @print("FAIL: get") return }
    Str h = strmod.pick(10)
    if !h.eq?("val3") { @print("FAIL: pick") return }
    @print("STRMC PASS")
}
