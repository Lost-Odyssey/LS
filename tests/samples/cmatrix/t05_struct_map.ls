// struct{map} drop
import std.map

struct Doc { Map(string,string) tags }
fn main() {
    Map(string,string) m = {}
    m.set("k".copy(),"v".copy())
    Doc d = Doc { tags: m }
    print(d.tags.has?("k"))
}
