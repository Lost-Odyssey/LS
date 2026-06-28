// operator_overload_memcheck.ls — operator method on has_drop struct (Str field).
// Verifies &Self rhs is not consumed and the returned owned struct leaks nothing.
import std.core.str

struct Name { Str first; int n }

methods Name: Add {
    def +(&self, &Name rhs) -> Name {
        return Name{ first: self.first.copy(), n: self.n + rhs.n }
    }
}

methods Name: Equal {
    def ==(&self, &Name rhs) -> bool { return self.n == rhs.n }
}

def main() {
    Name a = Name{ first: "alice", n: 1 }
    Name b = Name{ first: "bob", n: 2 }
    Name c = a + b
    @print(c.first)
    @print(c.n)
    @print(a == b)
    @print(a != b)
    // a and b still alive (borrowed, not moved)
    @print(a.first)
    @print(b.first)
}
