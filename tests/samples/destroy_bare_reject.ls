// negative: `~` destructor is only allowed inside a `methods X: Destroy {}` block,
// not a bare inherent `methods X {}` block.
struct F { int x }
methods F {
    def ~(&!self) { }
}
def main() -> int { return 0 }
