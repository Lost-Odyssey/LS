/* Smoke test: split + join. */
import std.vec
import std.string

fn main() -> int {
    string s = "a,b,c,d"
    Vec(string) parts = s.split(",")
    print(parts.len())
    for i in 0..parts.len() {
        print(parts[i])
    }
    string back = ",".join(parts)
    print(back)
    return 0
}
