// D: mutate struct field vec through &! — must persist + be clean
import std.vec

struct Doc { Vec(int) items }
fn add(&!Doc d, int x) { d.items.push(x) }
fn main() {
    Vec(int) v = {}
    Doc d = Doc { items: v }
    add(&!d, 10); add(&!d, 20)
    print(d.items.len())    // expect 2
}
