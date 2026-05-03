// Phase 4 __move() — primary use case: user-defined function arguments.
//   - Default:  fn(struct_with_drop) takes a DEEP CLONE; caller retains ownership.
//   - __move:   fn(__move(p))       TRANSFERS ownership; caller cannot use p again.
//
// For built-in container ops (vec.push / map.set / ...), ownership is already
// transferred unconditionally, so `vec.push(x)` and `vec.push(__move(x))` are
// identical at both checker and codegen level.
struct Person { string name; int age; }

fn consume(Person who) {
    // `who` is owned by this function. Its strings will be freed at scope exit.
    print(who.name)
}

fn inspect(Person who) {
    // Default clone semantics — the caller keeps its own copy.
    print(who.age)
}

fn main() -> int {
    // --- DEFAULT: clone ---
    Person a
    a.name = "Alice".upper()
    a.age = 30
    inspect(a)         // deep-clones a; a still LIVE here
    inspect(a)         // OK, still LIVE
    print(a.name)      // still readable

    // --- __move: transfer ownership ---
    Person b
    b.name = "Bob".upper()
    b.age = 40
    consume(__move(b)) // ownership transferred to `consume`; b becomes MOVED
    // print(b.name)   // would be a checker error: use of moved variable 'b'

    return 0
}
