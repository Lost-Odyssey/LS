// Phase C.7 (revised): Vec(T), Map(K,V), and struct(has_drop) captures are
// by-move (env owns the value).

import std.vec
import std.map
import std.str

type IntGetter  = Block(int) -> int
type StrBuilder = Block(Str) -> Str
type MapLookup  = Block(Str) -> int
type Stamper    = Block(Str) -> Str

struct Tag {
    Str name
    int weight
}

fn make_stamper(Tag t) -> Stamper {
    return |sep| f"{t.name}{sep}{t.weight}"
}

fn main() {
    // 1) Vec(int) by-move capture: outer is moved into closure.
    Vec(int) nums = [10, 20, 30]
    IntGetter pick = |i| nums[i]
    print(pick(0))          // 10
    print(pick(2))          // 30
    // nums was moved into the closure; post-capture mutations are not possible.
    // The closure owns its own copy.

    // 2) Vec(Str) by-move capture: inline closure.
    Vec(Str) words = ["hello", "world"]
    StrBuilder joiner = |sep| {
        Str out = ""
        int i = 0
        while i < words.len() {
            if i > 0 { out = out + sep }
            out = out + words[i]
            i = i + 1
        }
        return out
    }
    print(joiner("-"))      // hello-world

    // 3) Map(Str, int) by-move capture.
    Map(Str, int) scores = {}
    scores.set("alice", 90)
    MapLookup look = |key| {
        match scores.get(key) {
            Some(v) => { return v }
            None => { return 0 }
        }
    }
    print(look("alice"))    // 90
    print(look("nobody"))   // 0

    // 4) struct(has_drop) by-move: factory pattern; outer moved into env.
    Str lvl = "lvl"
    Tag t = Tag { name: lvl.upper(), weight: 7 }
    Stamper st = make_stamper(t)
    print(st(":"))          // LVL:7
}
