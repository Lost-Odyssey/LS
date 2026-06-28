enum Cell { Empty, Value(int) }

methods Cell {
    def set(&!self, int v) {
        self = Value(v);
    }
    def get(&self) -> int {
        match self { Value(v) => v, _ => 0 }
    }
}

def main() {
    Cell c = Empty;
    if (c.get() == 0) { @print("PASS 2a") }
    c.set(42);
    if (c.get() == 42) { @print("PASS 2b") }
}
