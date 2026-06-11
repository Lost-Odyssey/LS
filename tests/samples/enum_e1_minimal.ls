import std.vec
import std.str

enum Data {
    Empty
    Text(Str s)
}

fn owned(Str x) -> Str { return x.copy() }

fn main() {
    Vec(Data) v4 = [Text(owned("d1")), Text(owned("d2"))]
    Vec(Data) v4c = v4.copy()
    print("D: copy len =", v4c.len())
    print("done")
}
