// Map(string,string) build + drop
import std.map

fn main() {
    Map(string,string) m = {}
    m.set("k".copy(), "v".copy())
    print(m.has?("k"))
}
