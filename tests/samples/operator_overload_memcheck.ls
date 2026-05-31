// operator_overload_memcheck.ls — operator method on has_drop struct (string field).
// Verifies &Self rhs is not consumed and the returned owned struct leaks nothing.

struct Name { string first; int n }

impl Add for Name {
    fn +(&self, &Name rhs) -> Name {
        return Name{ first: self.first.copy(), n: self.n + rhs.n }
    }
}

impl Eq for Name {
    fn ==(&self, &Name rhs) -> bool { return self.n == rhs.n }
}

fn main() {
    Name a = Name{ first: "alice", n: 1 }
    Name b = Name{ first: "bob", n: 2 }
    Name c = a + b
    print(c.first)
    print(c.n)
    print(a == b)
    print(a != b)
    // a and b still alive (borrowed, not moved)
    print(a.first)
    print(b.first)
}
