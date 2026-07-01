// struct{map} drop
import std.core.map

struct Doc { Map(Str,Str) tags }
def main() {
    Map(Str,Str) m = {}
    m.set("k".copy(),"v".copy())
    Doc d = Doc { tags: m }
    @print(d.tags.has?("k"))
}
