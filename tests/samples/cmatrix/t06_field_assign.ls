// D: assign a new vec to a struct field
struct Doc { vec(int) items }
fn main() {
    vec(int) v = [1]
    Doc d = Doc { items: v }
    vec(int) w = [9,9,9]
    d.items = w
    print(d.items.length)   // expect 3
}
