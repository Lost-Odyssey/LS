fn make_opt() -> Option(vec(string)) {
    vec(string) v = []
    v.push("hello")
    v.push("world")
    return Some(v)
}

fn main() {
    print("a")
    Option(vec(string)) r = make_opt()
    print("b")
    match r {
        Some(v) => { print(f"len={v.length}") }
        None    => { print("none") }
    }
    print("done")
}
