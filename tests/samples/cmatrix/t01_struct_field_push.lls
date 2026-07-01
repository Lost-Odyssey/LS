// D: mutate struct field vec through &! — must persist + be clean
import std.core.vec

struct Doc { Vec(int) items }
def add(&!Doc d, int x) { d.items.push(x) }
def main() {
    Vec(int) v = {}
    Doc d = Doc { items: v }
    add(&!d, 10); add(&!d, 20)
    @print(d.items.len())    // expect 2
}
