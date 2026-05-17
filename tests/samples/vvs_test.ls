fn main() {
    vec(vec(string)) all = []
    vec(string) a = []
    a.push("hello")
    a.push("world")
    vec(string) b = []
    b.push("foo")
    b.push("bar")
    all.push(a)
    all.push(b)
    print(f"len={all.length}")
    vec(string) r0 = all[0]
    vec(string) r1 = all[1]
    print(r0[0])
    print(r0[1])
    print(r1[0])
    print(r1[1])
    print("done")
}
