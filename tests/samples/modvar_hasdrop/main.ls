import mod_a
import std.str

fn main() -> int {
    Str g = mod_a.get_greeting()
    print(f"greeting={g}")
    if g.eq?("hello") {
        print("MODVAR_HASDROP PASS")
    }
    return 0
}
