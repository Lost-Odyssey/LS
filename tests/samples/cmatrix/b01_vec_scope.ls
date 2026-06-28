// Vec(Str) build + scope drop
import std.core.vec

def main() {
    Vec(Str) v = {}
    v.push("a".copy()); v.push("b".copy())
    @print(v.len())
}
