import std.map

fn main() {
    Map(string, string) m = {}
    string v = "hel" + "lo"
    m.set("key1", v)
    m.set("key2", "world")
    print(m.len())
}
