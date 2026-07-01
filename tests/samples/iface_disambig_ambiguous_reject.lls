// Negative: bare `p.close()` is ambiguous — no inherent provider and two
// interfaces (Source, Sink) provide `close`. Expected: "ambiguous method".
interface Source { def close(&!self) -> int }
interface Sink   { def close(&!self) -> int }
struct Pipe { int n }
methods Pipe: Source { def close(&!self) -> int { return 1 } }
methods Pipe: Sink   { def close(&!self) -> int { return 2 } }
def main() -> int {
    Pipe p = Pipe { n: 0 }
    return p.close()   // ambiguous
}
