// Phase E.1: closure body captured vec/map/struct passed to fn — clone fallback.
// Verifies that double-free is prevented when a borrowed closure capture is
// passed to a value-ABI function parameter.
//
// With by-ref capture semantics (Phase E), the outer vec/map is NOT moved.
// After the closure is defined, the outer variable remains fully usable.

type Adder      = Block() -> int
type MapCounter = Block() -> int

fn sum_int_vec(vec(int) v) -> int {
    int s = 0
    int i = 0
    while i < v.length {
        s = s + v[i]
        i = i + 1
    }
    return s
}

fn vec_len(vec(int) v) -> int {
    return v.length
}

fn count_keys(map(string, int) m) -> int {
    return m.length
}

fn main() {
    // E.1.1: closure captures vec(int), body passes to fn; verify correct value
    vec(int) nums = [10, 20, 30]
    Adder adder = || {
        return sum_int_vec(nums)
    }
    int r1 = adder()
    print(r1)               // 60
    // Outer nums is still live (by-ref capture), push and call again
    nums.push(40)
    int r1b = adder()
    print(r1b)              // 100

    // E.1.2: closure captures vec(int), body passes to fn returning length
    vec(int) items = [1, 2, 3, 4, 5]
    Adder counter = || {
        return vec_len(items)
    }
    int r2 = counter()
    print(r2)               // 5

    // E.1.3: multiple calls with same closure — original capture stays intact
    vec(int) data = [7, 8, 9]
    Adder summer = || {
        return sum_int_vec(data)
    }
    print(summer())         // 24
    print(summer())         // 24

    // E.1.4: closure captures map(string,int), body passes to fn
    map(string, int) scores = {}
    scores["alice"] = 42
    scores["bob"] = 99
    MapCounter key_counter = || {
        return count_keys(scores)
    }
    print(key_counter())    // 2
    print(key_counter())    // 2
}
