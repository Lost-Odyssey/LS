// Phase 8: built-in Option(T) and Result(T, E)
// Exercises template instantiation + context-driven variant disambiguation
// (Some can mean Option(int).Some or Option(Str).Some — both used here).

import std.core.str

def safe_div(int n, int d) -> Result(int, Str) {
    match d {
        0 => Err("divide by zero")
        _ => Ok(n / d)
    }
}

def main() -> int {
    Option(int) a = Some(42)
    Option(int) b = None
    Option(Str) c = Some("hi")
    Option(Str) d = None

    match a { Some(x) => @print(x)  None => @print(-1) }       // 42
    match b { Some(x) => @print(x)  None => @print(-1) }       // -1
    match c { Some(x) => @print(x)  None => @print("none") }   // hi
    match d { Some(x) => @print(x)  None => @print("none") }   // none

    Result(int, Str) r1 = safe_div(20, 4)
    Result(int, Str) r2 = safe_div(7, 0)
    match r1 { Ok(v) => @print(v)  Err(m) => @print(m) }       // 5
    match r2 { Ok(v) => @print(v)  Err(m) => @print(m) }       // divide by zero
    return 0
}
