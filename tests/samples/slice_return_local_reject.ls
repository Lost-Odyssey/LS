// Slice returns are allowed only under single-input lifetime elision (the view
// must borrow the one input). An `&self` method returning a view of a LOCAL Vec
// would dangle once the method returns → must be rejected.
import std.core.vec

struct Buf { Vec(int) data }

methods Buf {
    def bad(&self) -> &array(int) {
        Vec(int) tmp = [9, 8, 7]
        return tmp[0..2]            // view of a local → would dangle
    }
}

def main() -> int {
    @print(1)
    return 0
}
