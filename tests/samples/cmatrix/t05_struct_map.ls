// struct{map} drop
import std.map

struct Doc { Map(Str,Str) tags }
fn main() {
    Map(Str,Str) m = {}
    m.set("k".copy(),"v".copy())
    Doc d = Doc { tags: m }
    print(d.tags.has?("k"))
}
