import std.core.map
import std.core.str
def main() -> int {
    Map(Str, int) m = { f"a": 1, f"b": 2 }   // requires the FromPairs protocol
    Str ka = f"a"
    Str kb = f"b"
    int a = m.get(ka).unwrap_or(0)
    int b = m.get(kb).unwrap_or(0)
    @print(f"a={a} b={b}")
    return 0
}
