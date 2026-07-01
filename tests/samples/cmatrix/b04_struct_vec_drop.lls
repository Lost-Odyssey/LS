// struct{Vec(Str)} build + scope drop (A)
import std.core.vec

struct Doc { Vec(Str) items }
def main() {
    Vec(Str) v = ["x","y"]
    Doc d = Doc { items: v }
    @print(d.items.len())
}
