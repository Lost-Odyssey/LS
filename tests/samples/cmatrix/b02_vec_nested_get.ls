// Vec(Vec(Str)) build + .get + drop (B)
import std.vec

fn main() {
    Vec(Vec(Str)) m = {}
    Vec(Str) r = ["a","b"]; m.push(r)
    Vec(Str) g = m.get!(0)
    print(g.len())
}
