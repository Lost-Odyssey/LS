enum Cell { Empty, Value(int) }

impl Cell {
    fn set(&!self, int v) {
        self = Value(v);
    }
    fn get(&self) -> int {
        match self { Value(v) => v, _ => 0 }
    }
}

fn main() {
    Cell c = Empty;
    if (c.get() == 0) { print("PASS 2a") }
    c.set(42);
    if (c.get() == 42) { print("PASS 2b") }
}
