import std.regex as re

fn main() {
    print("a: capture")
    vec(string) caps = re.capture("2024-01-15", "(\\d{4})-(\\d{2})-(\\d{2})")
    print("b")
    if caps.length == 0 { print("none"); return }
    print(f"len={caps.length}")
    print(caps[0])
    print(caps[1])
    print("c: capture_all")
    vec(string) all = re.capture_all("a=1 b=2", "(\\w+)=(\\d+)")
    print(f"all.length={all.length}")
    print("done")
}
