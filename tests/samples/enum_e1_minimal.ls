import std.vec

enum Data {
    Empty
    Text(string s)
}

fn main() {
    Vec(Data) v4 = [Text("d1".copy()), Text("d2".copy())]
    Vec(Data) v4c = v4.copy()
    print("D: copy len =", v4c.len())
    print("done")
}
