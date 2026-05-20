// trait_sig_mismatch.ls — negative test: signature mismatch in trait impl

trait Comparable {
    fn compare(&self, int other) -> int
}

struct Point {
    int x
    int y
}

// Wrong parameter count (missing 'other')
impl Comparable for Point {
    fn compare(&self) -> int {
        return self.x
    }
}

fn main() {
    print(42)
}
