// tree.ls — a module exposing an enum whose payload is a std.vec Vec(T).
// Consumers pattern-match the payload and call Vec methods on the binder.
module tree
import std.vec

enum Tree {
    Node(Vec(int) kids)
    Labels(Vec(string) names)   // has_drop element type (string)
    Leaf(int value)
}

fn make() -> Tree {
    Vec(int) v = [10, 20, 30]
    return Node(v)
}

fn make_labels() -> Tree {
    Vec(string) v = {}
    v.push("alpha".upper())
    v.push("beta".upper())
    return Labels(v)
}
