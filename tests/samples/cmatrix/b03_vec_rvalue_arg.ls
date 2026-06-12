// f(v.get!(i)) nested-vec rvalue arg (E)
import std.vec

fn take(Vec(Str) v) -> int { return v.len() }
fn main() {
    Vec(Vec(Str)) m = {}
    Vec(Str) r = ["a","b"]; m.push(r)
    print(take(m.get!(0)))
}
