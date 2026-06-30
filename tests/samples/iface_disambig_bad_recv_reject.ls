// Negative: `Sink.close(&!p)` but Pipe does not implement Sink.
// Expected: "interface 'Sink' has no method 'close' for type 'Pipe'".
interface Source { def close(&!self) -> int }
interface Sink   { def close(&!self) -> int }
struct Pipe { int n }
methods Pipe: Source { def close(&!self) -> int { return 1 } }
def main() -> int {
    Pipe p = Pipe { n: 0 }
    return Sink.close(&!p)   // Pipe does not implement Sink
}
