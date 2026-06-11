// struct{Vec(Str)} build + scope drop (A)
import std.vec

struct Doc { Vec(Str) items }
fn main() {
    Vec(Str) v = ["x","y"]
    Doc d = Doc { items: v }
    print(d.items.len())
}
