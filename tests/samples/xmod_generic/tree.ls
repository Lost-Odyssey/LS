// tree.ls — a module exposing an enum whose payload is a std.core.vec Vec(T).
// Consumers pattern-match the payload and call Vec methods on the binder.
module tree
import std.core.str
import std.core.vec

enum Tree {
    Node(Vec(int) kids)
    Labels(Vec(Str) names)   // has_drop element type (Str)
    Leaf(int value)
}

def make() -> Tree {
    Vec(int) v = [10, 20, 30]
    return Node(v)
}

def make_labels() -> Tree {
    Vec(Str) v = {}
    v.push("alpha".upper())
    v.push("beta".upper())
    return Labels(v)
}
