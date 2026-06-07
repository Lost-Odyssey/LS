import std.vec
import std.regex as re

fn main() {
    print("a")
    Vec(string) caps = re.capture("2024", "(\\d+)")
    print("b")
    if caps.len() == 0 { print("none"); return }
    print(f"len={caps.len()}")
    print("done")
}
