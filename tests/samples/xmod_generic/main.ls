import tree

// Calls a Vec(Str) method on an imported-enum payload binder. Defining this
// triggers consumer-side instantiation of Vec(Str) (VR-LIM-018 path). The
// F6 has_drop-propagation fix ensures that does NOT leave a *separately*
// instantiated Vec(Str) (e.g. `local` in main) without has_drop → leak.
fn label_count(Tree t) -> int {
    match t {
        Labels(names) => { return names.len() }
        _ => { return 0 }
    }
}

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
        _ => { print("other") }
    }

    // has_drop Vec(Str) across the module boundary: build via the module,
    // count via a binder method, then own a local Vec(Str) that MUST be
    // dropped (its Str elements freed) at scope exit. Regression for the F6
    // has_drop-propagation bug (cross-module method call left the local
    // instance unmarked has_drop → leaked).
    Tree lt = tree.make_labels()
    int lc = label_count(lt)
    Vec(Str) local = {}
    local.push("gamma".upper())
    local.push("delta".upper())
    print(f"labels={lc} local={local.len()} first={local.get(0)}")
    if lc == 2 && local.len() == 2 {
        print("XMOD_GENERIC_DROP PASS")
    }
    return 0
}
