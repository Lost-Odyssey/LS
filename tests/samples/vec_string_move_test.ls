import std.vec

fn main() -> int {
    Vec(Str) v = {}
    Str s = "hello".upper()
    v.push(s)
    Str t = "world".lower()
    v.push(t)
    print(v[0])
    print(v[1])
    return 0
}
