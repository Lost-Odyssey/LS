module mod_a
import std.core.str

Str greeting = "hello"

def get_greeting() -> Str {
    return greeting
}

def set_greeting(Str s) {
    greeting = s
}
