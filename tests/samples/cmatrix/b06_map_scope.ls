// map(string,string) build + drop
fn main() {
    map(string,string) m = {}
    m.set("k".copy(), "v".copy())
    print(m.contains_key("k"))
}
