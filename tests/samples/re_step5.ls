import std.vec

fn make_opt() -> Option(Vec(Str)) {
    Vec(Str) v = {}
    v.push("hello")
    v.push("world")
    return Some(v)
}

fn main() {
    print("a")
    Option(Vec(Str)) r = make_opt()
    print("b")
    match r {
        Some(v) => { print(f"len={v.len()}") }
        None    => { print("none") }
    }
    print("done")
}
