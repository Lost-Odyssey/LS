// std/hello.ls — Phase E.3.4 stdlib path test module

import std.str

fn greet(Str name) -> Str {
    Str g = "Hello from stdlib, "
    Str bang = "!"
    g.push_str(name)
    g.push_str(bang)
    return g
}

fn answer() -> int {
    return 42
}
