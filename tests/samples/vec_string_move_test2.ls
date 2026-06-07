import std.vec

fn main() -> int {
    Vec(string) v = {}
    string name = "Alice"
    v.push(name)
    string city = "Beijing"
    v.push(city)
    print(v[0])
    print(v[1])
    return 0
}
