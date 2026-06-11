import std.vec

fn main() -> int {
    Vec(Str) v = {}
    Str name = "Alice"
    v.push(name)
    Str city = "Beijing"
    v.push(city)
    print(v[0])
    print(v[1])
    return 0
}
