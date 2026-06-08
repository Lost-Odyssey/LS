// Vec(string) build + scope drop
import std.vec

fn main() {
    Vec(string) v = {}
    v.push("a".copy()); v.push("b".copy())
    print(v.len())
}
