import std.vec

// Phase 3 struct move semantics â€?valid program that exercises:
//   - field-level assignment does not move the struct
//   - POD struct (no has_drop) follows clone semantics
//   - vec.push on has_drop struct transfers ownership
struct Pod { int x; int y; }
struct Person { string name; int age; }

fn main() -> int {
    // --- POD struct: clone semantics, source remains usable. ---
    Pod a = Pod{ x: 1, y: 2 }
    Pod b = a
    print(a.x)
    print(b.y)

    // --- has_drop struct: field writes do NOT move the struct. ---
    Person p
    p.name = "Alice".upper()
    p.age = 30
    p.name = "Bob".upper()      // overwrite field â€?still OK
    print(p.name)
    print(p.age)

    // --- vec.push transfers ownership of the struct. ---
    Vec(Person) people = {}
    Person q
    q.name = "Carol".upper()
    q.age = 40
    people.push(q)              // q is now MOVED; must not be used again
    print(people.empty?())

    return 0
}
