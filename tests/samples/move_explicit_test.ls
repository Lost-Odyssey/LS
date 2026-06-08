import std.vec

// Phase 4 __move() explicit move annotation â€?end-to-end test.
// Covers:
//   - __move on a dynamic string transferred to vec.push
//   - __move forcing a static string into vec.push (checker allows it)
//   - __move on a has_drop struct transferred to vec.push
//   - __move in a simple assignment of string
struct Person { string name; int age; }

fn main() -> int {
    // --- dynamic string: __move is equivalent to implicit move, no double-free ---
    Vec(string) names = {}
    string a = "alice".upper()
    names.push(__move(a))        // explicit move; `a` becomes MOVED
    print(names.empty?())       // false

    // --- static string forced to move ---
    Vec(string) tags = {}
    string t = "rust"             // static (cap==0)
    tags.push(__move(t))          // __move forces move; runtime: static stays static
    print(tags.empty?())        // false

    // --- has_drop struct: __move into a vec ---
    Vec(Person) people = {}
    Person p
    p.name = "bob".upper()
    p.age = 42
    people.push(__move(p))        // p: MOVED; scope cleanup skips p's string drop
    print(people.empty?())      // false

    // --- __move in a simple variable assignment ---
    string src = "hello".upper()
    string dst = __move(src)      // dst clones (codegen) while src becomes MOVED
    print(dst)                    // HELLO

    return 0
}
