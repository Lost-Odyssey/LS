// enum_vec_payload_test.ls — test enum with vec/map payload drop
// This verifies L-006 (enum containing vec/map payload) is actually working

import std.vec
import std.map

enum Data {
    Empty
    Numbers(Vec(int) nums)
    Lookup(Map(string, int) table)
    Mixed(string label, Vec(string) items)
}

fn make_numbers() -> Data {
    Vec(int) v = [10, 20, 30]
    return Numbers(v)
}

fn make_lookup() -> Data {
    Map(string, int) m = {}
    m.set("a", 1)
    m.set("b", 2)
    return Lookup(m)
}

fn make_mixed() -> Data {
    Vec(string) items = {}
    items.push("hello")
    items.push("world")
    return Mixed("test", items)
}

fn process(Data d) {
    match d {
        Empty => { print("empty") }
        Numbers(nums) => { print(f"numbers: len={nums.len()}") }
        Lookup(table) => {
            bool has_a = table.has?("a")
            print(f"lookup: has_a={has_a}")
        }
        Mixed(label, items) => { print(f"mixed: {label} len={items.len()}") }
    }
}

fn main() {
    Data d1 = make_numbers()
    process(d1)

    Data d2 = make_lookup()
    process(d2)

    Data d3 = make_mixed()
    process(d3)

    Data d4 = Empty
    process(d4)

    print("done")
}
