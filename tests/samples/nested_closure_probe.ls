type Inner = Block(int) -> int
type Outer = Block(int) -> Inner

def make_adder(int base) -> Outer {
    return |x| {
        return |y| { return x + y + base }
    }
}

def main() {
    Outer f = make_adder(10)
    Inner g = f(5)
    @print(g(3))
}
