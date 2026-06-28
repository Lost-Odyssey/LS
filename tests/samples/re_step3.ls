import std.core.vec
import std.text.regex as re
import std.core.str

def main() {
    @print("a: capture")
    Vec(Str) caps = re.capture("2024-01-15", "(\\d{4})-(\\d{2})-(\\d{2})")
    @print("b")
    if caps.len() == 0 { @print("none"); return }
    @print(f"len={caps.len()}")
    @print(caps[0])
    @print(caps[1])
    @print("c: capture_all")
    Vec(Str) all = re.capture_all("a=1 b=2", "(\\w+)=(\\d+)")
    @print(f"all.len={all.len()}")
    @print("done")
}
