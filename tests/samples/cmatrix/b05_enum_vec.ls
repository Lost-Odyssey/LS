// enum with Vec(Str) payload build+drop (json-style)
import std.core.vec
enum E { Items(Vec(Str)) None }
def main() {
    Vec(Str) v = ["x","y"]
    E e = Items(v)
    match e { Items(xs) => @print(xs.len()), None => @print(0) }
}
