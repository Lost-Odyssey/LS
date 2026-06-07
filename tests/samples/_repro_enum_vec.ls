import std.vec

enum Numbers {
    Empty
    Nums(Vec(int) nums)
    Mixed(string label, Vec(string) items)
}

fn make_numbers() -> Numbers {
    Vec(int) v = [10, 20, 30]
    return Nums(v)
}

fn make_mixed() -> Numbers {
    Vec(string) items = []
    items.push("hello")
    items.push("world")
    return Mixed("test", items)
}

fn process(Numbers d) {
    match d {
        Empty => { print("empty") }
        Nums(nums) => { print(f"numbers: len={nums.len()}") }
        Mixed(label, items) => { print(f"mixed: {label} len={items.len()}") }
    }
}

fn main() {
    Numbers d1 = make_numbers()
    process(d1)

    Numbers d3 = make_mixed()
    process(d3)

    Numbers d4 = Empty
    process(d4)

    print("done")
}
