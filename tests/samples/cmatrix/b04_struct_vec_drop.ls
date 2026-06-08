// struct{Vec(string)} build + scope drop (A)
import std.vec

struct Doc { Vec(string) items }
fn main() {
    Vec(string) v = ["x","y"]
    Doc d = Doc { items: v }
    print(d.items.len())
}
