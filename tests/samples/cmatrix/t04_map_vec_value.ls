// map value = vec (nested container in map)
import std.vec
import std.map

fn main() {
    Map(Str, Vec(int)) m = {}
    Vec(int) v = [1,2,3]
    m.set("a".copy(), v)
    print(m.has?("a"))
}
