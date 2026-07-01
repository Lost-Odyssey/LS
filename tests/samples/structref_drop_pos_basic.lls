/* Phase B: drop struct (含 Str) 借用基础测试。 */
struct Person { Str name; int age; }

def show(&Person p) {
    @print(p.name)
    @print(p.age)
}

def rename(&!Person p, Str n) {
    p.name = n
}

def main() -> int {
    Person q = Person { name: "Alice", age: 30 }
    show(q)
    rename(&!q, "Bob")
    show(q)
    return 0
}
