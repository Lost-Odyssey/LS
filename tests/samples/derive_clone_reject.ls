// Negative: @derive(Clone) is redundant in LS (auto-clone) and must be rejected
// with a clear explanation rather than silently accepted.
@derive(Clone)
struct B { int x }
def main() { @print("unreachable") }
