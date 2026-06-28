// Map(Str,Str) build + drop
import std.core.map

def main() {
    Map(Str,Str) m = {}
    m.set("k".copy(), "v".copy())
    @print(m.has?("k"))
}
