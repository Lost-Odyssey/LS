// vec_global_drop_test.ls — VR-LIM-002: global user Vec(T) must run __drop.

import std.vec
import std.str

Vec(int) gi = {}
Vec(Str) gs = {}

struct Item { Str name }

impl Item {
    static fn make(Str s) -> Item { return Item { name: s } }
}

Vec(Item) gitems = {}

fn fill_globals() {
    gi.push(10)
    gi.push(20)
    gs.push("alpha".upper())
    gs.push("beta".upper())
    gitems.push(Item.make("one".upper()))
    gitems.push(Item.make("two".upper()))
}

fn check(bool c, Str label) -> bool {
    if c { return true }
    print(f"FAIL {label}")
    return false
}

fn main() {
    fill_globals()
    bool ok = true
    ok = check(gi.len() == 2 && gi.get(1) == 20, "global Vec(int)") && ok
    ok = check(gs.len() == 2 && gs.get(0).eq?("ALPHA"), "global Vec(Str)") && ok
    Item item = gitems.get(1)
    ok = check(gitems.len() == 2 && item.name.eq?("TWO"), "global Vec(Item)") && ok
    if ok { print("VEC_GLOBAL_DROP PASS") }
}
