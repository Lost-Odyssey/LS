import std.vec

fn main() {
    Vec(Vec(string)) all = {}
    Vec(string) a = {}
    a.push("hello")
    a.push("world")
    Vec(string) b = {}
    b.push("foo")
    b.push("bar")
    all.push(a)
    all.push(b)
    print(f"len={all.len()}")
    Vec(string) r0 = all[0]
    Vec(string) r1 = all[1]
    print(r0[0])
    print(r0[1])
    print(r1[0])
    print(r1[1])
    print("done")
}
