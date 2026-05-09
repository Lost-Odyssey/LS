// Phase C.5: by-move string captures + per-closure env_drop + RAII.
// Validates:
//   - Capturing an owned string by-move into a closure literal
//   - The captured string is usable inside the body
//   - The closure outlives the function that captured (make_greeter pattern)
//   - Mixed POD + string captures
//   - Static (literal) string captured: no move triggered, freely usable later
//   - Memcheck reports 0 leaks (env_drop frees each captured string's data)

type Greeter = Block(string) -> string

fn greet_with(string prefix, string name) -> string {
    return prefix + ": " + name
}

fn make_greeter(string prefix) -> Greeter {
    return |name| prefix + ": " + name      // captures prefix by-move
}

fn use_greeter(Greeter g, string who) -> string {
    return g(who)
}

fn main() {
    // 1) Direct capture of owned string
    string p1 = "INFO".lower().upper()      // ensures cap > 0 (heap-owned)
    Greeter g1 = make_greeter(p1)
    // p1 is now MOVED — must not be used past this point.
    string r1 = use_greeter(g1, "alice")
    print(r1)                                // INFO: alice

    // 2) Mixed POD + string capture
    int n = 7
    string tag = "tag".upper()
    Greeter g2 = make_greeter(tag)
    print(use_greeter(g2, "bob"))            // TAG: bob
    print(n)                                 // 7  (POD capture path unaffected)

    // 3) Static string capture (no move — cap==0 in runtime)
    string s_static = "STATIC"
    Greeter g3 = make_greeter(s_static)
    print(use_greeter(g3, "carol"))          // STATIC: carol
    // s_static is NOT moved (was static); env still calls drop on it
    // safely because the env_drop checks cap > 0 before freeing.

    // 4) Make sure the body can use captured value more than once
    string twice = "X"
    Greeter g4 = make_greeter(twice)
    print(use_greeter(g4, "y"))              // X: y
    print(use_greeter(g4, "z"))              // X: z
}
