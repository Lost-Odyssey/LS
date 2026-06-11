import std.vec

// Phase 4 __move() explicit move annotation â€?end-to-end test.
// Covers:
//   - __move on a dynamic Str transferred to vec.push
//   - __move forcing a static Str into vec.push (checker allows it)
//   - __move on a has_drop struct transferred to vec.push
//   - __move in a simple assignment of Str
struct Person { Str name; int age; }

fn main() -> int {
    // --- dynamic Str: __move is equivalent to implicit move, no double-free ---
    Vec(Str) names = {}
    Str a = "alice".upper()
    names.push(__move(a))        // explicit move; `a` becomes MOVED
    print(names.empty?())       // false

    // --- static Str forced to move ---
    Vec(Str) tags = {}
    Str t = "rust"             // static (cap==0)
    tags.push(__move(t))          // __move forces move; runtime: static stays static
    print(tags.empty?())        // false

    // --- has_drop struct: __move into a vec ---
    Vec(Person) people = {}
    Person p
    p.name = "bob".upper()
    p.age = 42
    people.push(__move(p))        // p: MOVED; scope cleanup skips p's Str drop
    print(people.empty?())      // false

    // --- __move in a simple variable assignment ---
    Str src = "hello".upper()
    Str dst = __move(src)      // dst clones (codegen) while src becomes MOVED
    print(dst)                    // HELLO

    return 0
}
