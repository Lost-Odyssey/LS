// Phase A: bare `Block(...)` in return position must be rejected with a hint
// pointing the user at type aliases.

fn make_adder(int n) -> Block(int) -> int {
    return 0
}

fn main() {
    print(0)
}
