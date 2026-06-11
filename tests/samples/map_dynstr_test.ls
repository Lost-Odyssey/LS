import std.map

fn main() {
    Map(Str, Str) m = {}
    Str v = "hel" + "lo"
    m.set("key1", v)
    m.set("key2", "world")
    print(m.len())
}
