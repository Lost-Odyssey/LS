// Negative: a qualified interface call must supply a receiver argument.
// Expected: "requires a receiver".
interface Source { def close(&!self) -> int }
struct Pipe { int n }
methods Pipe: Source { def close(&!self) -> int { return 1 } }
def main() -> int {
    return Source.close()   // missing receiver
}
