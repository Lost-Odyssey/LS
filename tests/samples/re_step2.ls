import std.vec
import std.regex as re

fn main() {
    print("a")
    Option(string) m = re.find("price: 42.5 USD", "\\d+\\.\\d+")
    print("b")
    match m {
        Some(s) => { print("find: " + s) }
        None    => { print("find: none") }
    }
    print("c")
    Vec(string) all = re.find_all("a1 b2 c3", "\\d+")
    print("d")
    print(f"find_all count: {all.len()}")
    print("e")
    int i = 0
    while i < all.len() {
        print(all[i])
        i = i + 1
    }
    print("done")
}
