// struct{vec(string)} build + scope drop (A)
struct Doc { vec(string) items }
fn main() {
    vec(string) v = ["x","y"]
    Doc d = Doc { items: v }
    print(d.items.length)
}
