// enum with Vec(string) payload build+drop (json-style)
import std.vec
enum E { Items(Vec(string)) None }
fn main() {
    Vec(string) v = ["x","y"]
    E e = Items(v)
    match e { Items(xs) => print(xs.len()), None => print(0) }
}
