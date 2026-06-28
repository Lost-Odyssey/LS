// map value = vec (nested container in map)
import std.core.vec
import std.core.map

def main() {
    Map(Str, Vec(int)) m = {}
    Vec(int) v = [1,2,3]
    m.set("a".copy(), v)
    @print(m.has?("a"))
}
