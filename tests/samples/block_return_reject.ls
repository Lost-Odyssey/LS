// Phase A: bare `Block(...)` in return position must be rejected with a hint
// pointing the user at type aliases.

def make_adder(int n) -> Block(int) -> int {
    return 0
}

def main() {
    @print(0)
}
