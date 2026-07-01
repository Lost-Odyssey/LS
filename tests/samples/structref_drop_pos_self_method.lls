/* Phase B: drop struct 上的 &self / &!self 方法。 */
struct Person { Str name; int age; }

methods Person {
    def greet(&self) {
        @print(self.name)
    }
    def rename(&!self, Str n) {
        self.name = n
    }
}

def through_ro(&Person p) {
    p.greet()
}

def through_mut(&!Person p, Str n) {
    p.rename(n)
    p.greet()  /* &!self -> downgrade to &self method */
}

def main() -> int {
    Person q = Person { name: "Alice", age: 30 }
    q.greet()
    through_ro(q)
    through_mut(&!q, "Bob")
    return 0
}
