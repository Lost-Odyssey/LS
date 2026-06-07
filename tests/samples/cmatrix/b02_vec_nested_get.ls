// vec(vec(string)) build + .get + drop (B)
import std.vec

fn main() {
    Vec(Vec(string)) m = {}
    Vec(string) r = ["a","b"]; m.push(r)
    Vec(string) g = m.get(0)
    print(g.len())
}
