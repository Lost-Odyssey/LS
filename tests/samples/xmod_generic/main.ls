import tree

fn main() -> int {
    Tree t = tree.make()
    match t {
        // VR-LIM-018: Vec(int) binder from an imported enum payload — method
        // calls (.len()/.get()) + index must resolve even though this module
        // never imported std.vec directly (transitive template pull).
        Node(kids) => {
            int n = kids.len()
            int a = kids.get(0)
            int b = kids[2]
            int sum = 0
            for x in kids { sum = sum + x }
            print(f"n={n} a={a} b={b} sum={sum}")
            if n == 3 && a == 10 && b == 30 && sum == 60 {
                print("XMOD_GENERIC PASS")
            }
        }
        Leaf(_) => { print("leaf") }
    }
    return 0
}
