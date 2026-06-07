import std.vec

fn main() -> int {
    Vec(string) v = {}
    string s = "hello".upper()
    v.push(s)
    string t = "world".lower()
    v.push(t)
    print(v[0])
    print(v[1])
    return 0
}
