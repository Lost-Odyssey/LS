// trait_sig_mismatch.ls — negative test: signature mismatch in trait impl

interface Comparable {
    def compare(&self, int other) -> int
}

struct Point {
    int x
    int y
}

// Wrong parameter count (missing 'other')
methods Point: Comparable {
    def compare(&self) -> int {
        return self.x
    }
}

def main() {
    @print(42)
}
