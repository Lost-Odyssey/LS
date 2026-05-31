// struct{map} drop
struct Doc { map(string,string) tags }
fn main() {
    map(string,string) m = {}
    m.set("k".copy(),"v".copy())
    Doc d = Doc { tags: m }
    print(d.tags.contains_key("k"))
}
