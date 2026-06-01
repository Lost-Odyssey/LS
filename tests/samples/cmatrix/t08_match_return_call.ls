// L-012 ③: a match arm whose body directly returns a heap-value call result
// (`return f(binding)`) must MOVE the temp out, not clone-and-leak it. The
// subject param is still dropped by normal scope cleanup. Must be clean.
enum Node {
    Leaf(vec(string) items)
    Empty
}

fn collect(vec(string) xs) -> vec(string) {
    vec(string) out = []
    int i = 0
    while i < xs.length { out.push(xs.get(i)); i = i + 1 }
    return out
}

fn links(Node n) -> vec(string) {
    match n {
        Leaf(items) => { return collect(items) }   // direct heap-value call return
        Empty       => { vec(string) e = []; return e }
    }
}

fn main() {
    vec(string) a = []
    a.push("x")
    a.push("y")
    Node n = Leaf(a)
    vec(string) r = links(n)
    print(f"len={r.length}")
}
