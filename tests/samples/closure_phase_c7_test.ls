// Phase C.7 (revised): Vec(T) captures are by-move (env owns the value);
// map(K,V) remains by-ref. struct(has_drop) also by-move.

import std.vec

type IntGetter  = Block(int) -> int
type StrBuilder = Block(string) -> string
type MapLookup  = Block(string) -> int
type Stamper    = Block(string) -> string

struct Tag {
    string name
    int weight
}

fn make_stamper(Tag t) -> Stamper {
    return |sep| t.name + sep + to_string(t.weight)
}

fn main() {
    // 1) Vec(int) by-move capture — outer is moved into closure
    Vec(int) nums = [10, 20, 30]
    IntGetter pick = |i| nums[i]
    print(pick(0))          // 10
    print(pick(2))          // 30
    // nums was moved into the closure; post-capture mutations not possible.
    // The closure owns its own copy.

    // 2) Vec(string) by-move capture — inline closure
    Vec(string) words = ["hello", "world"]
    StrBuilder joiner = |sep| {
        string out = ""
        int i = 0
        while i < words.len() {
            if i > 0 { out = out + sep }
            out = out + words[i]
            i = i + 1
        }
        return out
    }
    print(joiner("-"))      // hello-world

    // 3) map(string, int) by-ref capture — outer still usable, new key visible
    map(string, int) scores = {}
    scores["alice"] = 90
    MapLookup look = |key| {
        if scores.contains_key(key) {
            return scores.get(key)
        }
        return 0
    }
    print(look("alice"))    // 90
    scores["bob"] = 75
    print(look("bob"))      // 75  ← key added after capture is visible
    print(look("nobody"))   // 0

    // 4) struct(has_drop) by-move — factory pattern; outer moved into env
    Tag t = Tag { name: "lvl".upper(), weight: 7 }
    Stamper st = make_stamper(t)
    print(st(":"))          // LVL:7
}
