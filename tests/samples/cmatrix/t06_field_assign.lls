// D: assign a new vec to a struct field
import std.core.vec

struct Doc { Vec(int) items }
def main() {
    Vec(int) v = [1]
    Doc d = Doc { items: v }
    Vec(int) w = [9,9,9]
    d.items = w
    @print(d.items.len())    // expect 3
}
