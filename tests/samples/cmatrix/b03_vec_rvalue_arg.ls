// f(v.get(i)) nested-vec rvalue arg (E)
import std.vec

fn take(Vec(string) v) -> int { return v.len() }
fn main() {
    Vec(Vec(string)) m = {}
    Vec(string) r = ["a","b"]; m.push(r)
    print(take(m.get(0)))
}
