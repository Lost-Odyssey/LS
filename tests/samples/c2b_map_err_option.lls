// C2b negative: map_err is a Result-only combinator; calling it on an Option
// is a compile-time error.
import std.core.str
def main() -> int {
    Option(int) o = Some(5)
    Option(int) bad = o.map_err(int)(|e| e)   // ERROR: Result combinator on Option
    return 0
}
