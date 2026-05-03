// Phase 8.5: try operator (Zig-style early return for Result/Option)
// Verifies:
//   - try unwraps Ok(T) into T on the success path
//   - try propagates Err(E) by early-returning Err(E) from the enclosing fn
//   - Same for Option(T): Some(T) unwraps, None propagates
//   - try chains in a sequence work for both happy and failing paths

fn parse_pos(int x) -> Result(int, string) {
    match x {
        0 => Err("zero")
        _ => Ok(x)
    }
}

fn double_pos(int x) -> Result(int, string) {
    int v = try parse_pos(x)
    return Ok(v * 2)
}

fn chain(int a, int b, int c) -> Result(int, string) {
    int x = try parse_pos(a)
    int y = try parse_pos(b)
    int z = try parse_pos(c)
    return Ok(x + y + z)
}

fn first_even(int x) -> Option(int) {
    match x % 2 {
        0 => Some(x)
        _ => None
    }
}

fn double_even(int x) -> Option(int) {
    int v = try first_even(x)
    return Some(v * 2)
}

fn main() -> int {
    // Result success path
    Result(int, string) r1 = double_pos(5)
    match r1 { Ok(v) => print(v)  Err(m) => print(m) }    // 10

    // Result failure path: Err propagates with original message
    Result(int, string) r2 = double_pos(0)
    match r2 { Ok(v) => print(v)  Err(m) => print(m) }    // zero

    // Chain all-success
    Result(int, string) r3 = chain(1, 2, 3)
    match r3 { Ok(v) => print(v)  Err(m) => print(m) }    // 6

    // Chain failure in the middle: stops at second try
    Result(int, string) r4 = chain(1, 0, 3)
    match r4 { Ok(v) => print(v)  Err(m) => print(m) }    // zero

    // Option success
    Option(int) o1 = double_even(4)
    match o1 { Some(v) => print(v)  None => print(-1) }   // 8

    // Option failure: None propagates
    Option(int) o2 = double_even(7)
    match o2 { Some(v) => print(v)  None => print(-1) }   // -1

    return 0
}
