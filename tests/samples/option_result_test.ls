// Phase 8: built-in Option(T) and Result(T, E)
// Exercises template instantiation + context-driven variant disambiguation
// (Some can mean Option(int).Some or Option(string).Some — both used here).

fn safe_div(int n, int d) -> Result(int, string) {
    match d {
        0 => Err("divide by zero")
        _ => Ok(n / d)
    }
}

fn main() -> int {
    Option(int) a = Some(42)
    Option(int) b = None
    Option(string) c = Some("hi")
    Option(string) d = None

    match a { Some(x) => print(x)  None => print(-1) }       // 42
    match b { Some(x) => print(x)  None => print(-1) }       // -1
    match c { Some(x) => print(x)  None => print("none") }   // hi
    match d { Some(x) => print(x)  None => print("none") }   // none

    Result(int, string) r1 = safe_div(20, 4)
    Result(int, string) r2 = safe_div(7, 0)
    match r1 { Ok(v) => print(v)  Err(m) => print(m) }       // 5
    match r2 { Ok(v) => print(v)  Err(m) => print(m) }       // divide by zero
    return 0
}
