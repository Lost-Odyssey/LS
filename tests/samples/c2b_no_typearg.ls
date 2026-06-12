// C2b negative: map/and_then/map_err require an explicit result type param
// (e.g. `opt.map(U)(|x| ..)`). Omitting it is a compile-time error.
import std.str
fn main() -> int {
    Option(int) o = Some(5)
    Option(int) bad = o.map(|x| x * 2)   // ERROR: missing type argument
    return 0
}
