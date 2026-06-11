import std.regex as re
import std.str

fn main() {
    bool r1 = re.matches("cat", "cat|dog")
    bool r2 = re.matches("dog", "cat|dog")
    bool r3 = re.matches("bird", "cat|dog")
    print(f"cat: {r1}")
    print(f"dog: {r2}")
    print(f"bird(should be false): {r3}")
    bool r4 = re.matches("hello", "[invalid")
    print(f"invalid pattern: {r4}")
    Option(Str) f = re.find("hello", "[invalid")
    match f {
        Some(s) => { print(f"find: Some {s}") }
        None => { print("find: None") }
    }
}
