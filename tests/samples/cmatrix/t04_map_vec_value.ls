// map value = vec (nested container in map)
import std.vec

fn main() {
    map(string, Vec(int)) m = {}
    Vec(int) v = [1,2,3]
    m.set("a".copy(), v)
    print(m.contains_key("a"))
}
