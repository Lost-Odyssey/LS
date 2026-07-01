import std.core.vec

// Phase 4 @move() explicit move annotation â€?end-to-end test.
// Covers:
//   - @move on a dynamic Str transferred to vec.push
//   - @move forcing a static Str into vec.push (checker allows it)
//   - @move on a has_drop struct transferred to vec.push
//   - @move in a simple assignment of Str
struct Person { Str name; int age; }

def main() -> int {
    // --- dynamic Str: @move is equivalent to implicit move, no double-free ---
    Vec(Str) names = {}
    Str a = "alice".upper()
    names.push(@move(a))        // explicit move; `a` becomes MOVED
    @print(names.empty?())       // false

    // --- static Str forced to move ---
    Vec(Str) tags = {}
    Str t = "rust"             // static (cap==0)
    tags.push(@move(t))          // @move forces move; runtime: static stays static
    @print(tags.empty?())        // false

    // --- has_drop struct: @move into a vec ---
    Vec(Person) people = {}
    Person p
    p.name = "bob".upper()
    p.age = 42
    people.push(@move(p))        // p: MOVED; scope cleanup skips p's Str drop
    @print(people.empty?())      // false

    // --- @move in a simple variable assignment ---
    Str src = "hello".upper()
    Str dst = @move(src)      // dst clones (codegen) while src becomes MOVED
    @print(dst)                    // HELLO

    return 0
}
