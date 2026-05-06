// Phase A: bare `Block(...)` in a struct field must be rejected.
struct Bus {
    Block(int) cb
}

fn main() { print(0) }
