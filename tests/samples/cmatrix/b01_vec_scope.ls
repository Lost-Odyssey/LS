// Vec(Str) build + scope drop
import std.vec

fn main() {
    Vec(Str) v = {}
    v.push("a".copy()); v.push("b".copy())
    print(v.len())
}
