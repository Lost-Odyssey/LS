import std.core.vec
import std.core.str

enum Data {
    Empty
    Text(Str s)
}

def owned(Str x) -> Str { return x.copy() }

def main() {
    Vec(Data) v4 = [Text(owned("d1")), Text(owned("d2"))]
    Vec(Data) v4c = v4.copy()
    @print("D: copy len =", v4c.len())
    @print("done")
}
