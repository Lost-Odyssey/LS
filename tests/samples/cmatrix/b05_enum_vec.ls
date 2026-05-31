// enum with vec(string) payload build+drop (json-style)
enum E { Items(vec(string)) None }
fn main() {
    vec(string) v = ["x","y"]
    E e = Items(v)
    match e { Items(xs) => print(xs.length), None => print(0) }
}
