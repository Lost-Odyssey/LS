// A user hand-writing the reserved `__from_list` method name must be rejected;
// the sanctioned form is `methods Bag: FromList { def from_list(...) }`.
struct Bag { int n }
methods Bag {
    def __from_list(&!self, int x) { self.n = self.n + 1 }
}
def main() -> int { return 0 }
