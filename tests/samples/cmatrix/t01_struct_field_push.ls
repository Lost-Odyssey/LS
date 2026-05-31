// D: mutate struct field vec through &! — must persist + be clean
struct Doc { vec(int) items }
fn add(&!Doc d, int x) { d.items.push(x) }
fn main() {
    vec(int) v = []
    Doc d = Doc { items: v }
    add(&!d, 10); add(&!d, 20)
    print(d.items.length)   // expect 2
}
