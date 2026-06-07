import std.vec

fn make_opt() -> Option(Vec(string)) {
    Vec(string) v = {}
    v.push("hello")
    v.push("world")
    return Some(v)
}

fn main() {
    print("a")
    Option(Vec(string)) r = make_opt()
    print("b")
    match r {
        Some(v) => { print(f"len={v.len()}") }
        None    => { print("none") }
    }
    print("done")
}
