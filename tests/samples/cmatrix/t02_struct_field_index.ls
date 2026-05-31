// D: read struct field vec element by [i]
struct Doc { vec(int) items }
fn main() {
    vec(int) v = [10,20]
    Doc d = Doc { items: v }
    print(d.items[0])       // "cannot get address of vec" now
}
