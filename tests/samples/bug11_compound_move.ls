import std.vec

fn main() -> int {
    Vec(string) vs = {}
    string str = ""
    for i in 0..4 {
        str += "a"
        vs.push(str)
    }
    print(str)
    return 0
}
