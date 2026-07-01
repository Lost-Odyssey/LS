// Negative: a generic free function whose type param appears in NO value-arg
// position cannot have its type args inferred — the call must say so clearly
// (it does not silently miscompile or crash). Explicit `pick(int)(1, 2)` works.

def pick(T)(int a, int b) -> int { return a + b }

def main() -> int {
    @print(pick(1, 2))   // <-- T cannot be inferred from int args
    return 0
}
