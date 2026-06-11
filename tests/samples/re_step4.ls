import std.vec
import std.regex as re
import std.str

fn main() {
    print("a")
    Vec(Str) caps = re.capture("2024", "(\\d+)")
    print("b")
    if caps.len() == 0 { print("none"); return }
    print(f"len={caps.len()}")
    print("done")
}
