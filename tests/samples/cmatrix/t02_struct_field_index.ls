// D: read struct field vec element by [i]
import std.core.vec

struct Doc { Vec(int) items }
def main() {
    Vec(int) v = [10,20]
    Doc d = Doc { items: v }
    @print(d.items[0])       // "cannot get address of vec" now
}
