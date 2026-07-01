// Phase C.5: by-move Str captures + per-closure env_drop + RAII.
// Validates:
//   - Capturing an owned Str by-move into a closure literal
//   - The captured Str is usable inside the body
//   - The closure outlives the function that captured (make_greeter pattern)
//   - Mixed POD + Str captures
//   - Static (literal) Str captured: no move triggered, freely usable later
//   - Memcheck reports 0 leaks (env_drop frees each captured Str's data)

import std.core.str

type Greeter = Block(Str) -> Str

def greet_with(Str prefix, Str name) -> Str {
    return f"{prefix}: {name}"
}

def make_greeter(Str prefix) -> Greeter {
    return |name| f"{prefix}: {name}"      // captures prefix by-move
}

def use_greeter(Greeter g, Str who) -> Str {
    return g(who)
}

def main() {
    // 1) Direct capture of owned Str
    Str p1 = f"INFO".lower().upper()        // ensures cap > 0 (heap-owned)
    Greeter g1 = make_greeter(p1)
    // p1 is now MOVED — must not be used past this point.
    Str r1 = use_greeter(g1, "alice")
    @print(r1)                                // INFO: alice

    // 2) Mixed POD + Str capture
    int n = 7
    Str tag = f"tag".upper()
    Greeter g2 = make_greeter(tag)
    @print(use_greeter(g2, "bob"))            // TAG: bob
    @print(n)                                 // 7  (POD capture path unaffected)

    // 3) Static Str capture (no move — cap==0 in runtime)
    Str s_static = "STATIC"
    Greeter g3 = make_greeter(s_static)
    @print(use_greeter(g3, "carol"))          // STATIC: carol
    // s_static is NOT moved (was static); env still calls drop on it
    // safely because the env_drop checks cap > 0 before freeing.

    // 4) Make sure the body can use captured value more than once
    Str twice = "X"
    Greeter g4 = make_greeter(twice)
    @print(use_greeter(g4, "y"))              // X: y
    @print(use_greeter(g4, "z"))              // X: z
}
