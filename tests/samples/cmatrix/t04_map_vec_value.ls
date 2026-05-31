// map value = vec (nested container in map)
fn main() {
    map(string, vec(int)) m = {}
    vec(int) v = [1,2,3]
    m.set("a".copy(), v)
    print(m.contains_key("a"))
}
