// Phase 8: self-recursive enum — Tree { Leaf  Node(int, Tree, Tree) }
// Exercises auto boxing of self-referential payload + recursive drop def.

enum Tree {
    Leaf
    Node(int value, Tree left, Tree right)
}

def sum(Tree t) -> int {
    match t {
        Leaf => 0
        Node(v, l, r) => v + sum(l) + sum(r)
    }
}

def main() -> int {
    Tree leaf = Leaf
    Tree leaf2 = Leaf
    Tree leaf3 = Leaf
    Tree leaf4 = Leaf
    Tree left = Node(2, leaf, leaf2)
    Tree right = Node(3, leaf3, leaf4)
    Tree root = Node(1, left, right)
    @print(sum(root))     // 6
    return 0
}
