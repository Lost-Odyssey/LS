// Phase C closures: POD captures + heap env + RAII.
// Validates:
//   - Capturing an outer int by-copy and using it inside the body
//   - Multiple captures of mixed POD types (int + bool + f64)
//   - Closure stored in a typed local var; env survives until scope end
//   - The classic make_adder pattern (returning a closure that closes over n)
//   - Captures don't shadow params with the same name

type Adder    = Block(int) -> int
type Mixer    = Block(int) -> f64
type Greeter  = Block(int) -> int

fn apply1(int x, Adder f) -> int {
    return f(x)
}

fn make_adder(int n) -> Adder {
    return |x| x + n          // captures n
}

fn make_mixer(int base, bool flip, f64 scale) -> Mixer {
    return |x| {
        int adj = x + base
        if flip { adj = 0 - adj }
        f64 r = adj as f64
        return r * scale
    }
}

fn main() {
    // Single int capture
    int outer = 100
    int r1 = apply1(7, |x| x + outer)
    print(r1)                 // 107

    // make_adder — closure outlives the function that built it
    Adder add5  = make_adder(5)
    Adder add10 = make_adder(10)
    print(add5(3))            // 8
    print(add10(3))           // 13
    print(add5(40))           // 45

    // Mixed-POD captures: int + bool + f64
    Mixer m = make_mixer(2, true, 1.5)
    f64 mr = m(8)
    print(mr)                 // (0 - (8+2)) * 1.5 = -15.0

    // Param shadows outer name — body sees the param, not the outer var
    int n = 999
    int r2 = apply1(4, |n| n + 1)
    print(r2)                 // 5  (param n=4, outer n=999 not captured)

    // Capture used twice in body
    int k = 6
    int r3 = apply1(0, |x| x + k + k)
    print(r3)                 // 12
}
