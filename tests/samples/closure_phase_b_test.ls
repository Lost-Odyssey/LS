// Phase B closures: no-capture lambda lifting + indirect call ABI.
// Validates:
//   - `|x| body` infers param types from a typed Block(...) parameter
//   - The lifted fn (`__closure_N(env, x)`) is callable through the fat ptr
//   - Both prefix and trailing-closure forms reach codegen
//   - Multi-param + zero-param + bool-returning closures all work

type Adder      = Block(int) -> int
type Pair       = Block(int, int) -> int
type Predicate  = Block(int) -> bool
type Producer   = Block() -> int

fn apply1(int x, Adder f) -> int {
    return f(x)
}

fn apply2(int x, int y, Pair f) -> int {
    return f(x, y)
}

fn pred_check(int x, Predicate f) -> bool {
    return f(x)
}

fn produce(Producer f) -> int {
    return f()
}

fn main() {
    // Prefix form
    int a = apply1(10, |x| x + 1)
    print(a)               // 11

    // Trailing closure form
    int b = apply1(20) { |x| x * 2 }
    print(b)               // 40

    // Multi-param
    int c = apply2(3, 4, |x, y| x * y)
    print(c)               // 12

    // Zero-arg
    int d = produce(|| 42)
    print(d)               // 42

    // Bool-returning
    bool e = pred_check(5, |x| x > 3)
    print(e)               // true

    // Multi-statement body via { } form
    int f = apply1(7) { |x|
        int t = x + 100
        return t
    }
    print(f)               // 107
}
