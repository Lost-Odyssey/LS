// enum_print_test.ls — @print(enum) renders readably as `Variant` /
// `Variant(payload, …)` (incl. Option/Result and enum fields inside structs).
// Before: print fell through to printf_fmt_for_type and emitted the raw
// discriminant/payload bytes (e.g. Option(Str) → `0000000000000001`) and leaked
// owned payloads. Driver greps the exact lines + runs memcheck (0/0/0).
import std.core.str

enum Color { Red, Green, Blue }
enum Shape { Circle(int), Rect(int, int), Named(Str) }
struct Wrap { Shape sh; Color c }

def some_str(int n) -> Option(Str) { return Some(f"s{n}") }

def main() -> int {
    @print(Red)                                  // Red
    @print(Blue)                                 // Blue
    @print(Circle(5))                            // Circle(5)
    @print(Rect(3, 4))                           // Rect(3, 4)
    @print(Named("hi"))                          // Named("hi")
    @print(some_str(7))                          // Some("s7")
    Option(int) n = None
    @print(n)                                    // None
    Result(int, Str) r = Err("bad")
    @print(r)                                     // Err("bad")
    @print(Wrap { sh: Rect(1, 2), c: Blue })     // Wrap{sh=Rect(1, 2), c=Blue}
    @print(some_str(8).map(Str)(|x| x))          // Some("s8")  (owned combinator rvalue, must not leak)
    @print("ENUMPRINT PASS")
    return 0
}
