import std.core.vec
import std.text.regex as re
import std.core.str

def main() {
    @print("a")
    Vec(Str) caps = re.capture("2024", "(\\d+)")
    @print("b")
    if caps.len() == 0 { @print("none"); return }
    @print(f"len={caps.len()}")
    @print("done")
}
