module mod_a
import std.str

Str greeting = "hello"

fn get_greeting() -> Str {
    return greeting
}

fn set_greeting(Str s) {
    greeting = s
}
