import std.regex as re

fn main() {
    bool ok = re.matches("hello world", "\\w+")
    if ok { print("ok") } else { print("fail") }
    print("done")
}
