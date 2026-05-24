enum Data {
    Empty
    Text(string s)
}

fn main() {
    vec(Data) v4 = [Text("d1".copy()), Text("d2".copy())]
    vec(Data) v4c = v4.copy()
    print("D: copy len =", v4c.length)
    print("done")
}
