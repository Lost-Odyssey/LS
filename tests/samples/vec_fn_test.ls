// Test in function
import std.vec

fn test() {
    Vec(Str) v = {}
    v.push("hello")
    v.pop()
    print("done")
}
test()
