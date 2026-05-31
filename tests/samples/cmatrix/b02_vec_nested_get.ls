// vec(vec(string)) build + .get + drop (B)
fn main() {
    vec(vec(string)) m = []
    vec(string) r = ["a","b"]; m.push(r)
    vec(string) g = m.get(0)
    print(g.length)
}
