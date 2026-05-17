import std.regex as re

fn main() {
    print("a")
    vec(string) caps = re.capture("2024", "(\\d+)")
    print("b")
    if caps.length == 0 { print("none"); return }
    print(f"len={caps.length}")
    print("done")
}
