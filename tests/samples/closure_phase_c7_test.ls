// Phase C.7 (revised): vec(T)/map(K,V) captures are by-ref (env stores a
// pointer to the outer alloca); struct(has_drop) remains by-move.
//
// Key differences from by-move semantics:
//   - The outer vec/map is NOT moved; it remains usable after capture.
//   - Mutations made to the outer vec/map after capture are visible inside
//     the closure body (live reference).
//   - Closure must not outlive the scope of the outer variable.
//
// struct(has_drop) still uses by-move (factory pattern works because the
// struct is fully transferred into the env).

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
    // 1) vec(int) by-ref capture — outer remains live; push visible in closure
    vec(int) nums = [10, 20, 30]
    IntGetter pick = |i| nums[i]
    print(pick(0))          // 10
    print(pick(2))          // 30
    nums.push(99)
    print(pick(3))          // 99  ← mutation made after capture is visible

    // 2) vec(string) by-ref capture — inline closure
    vec(string) words = ["hello", "world"]
    StrBuilder joiner = |sep| {
        string out = ""
        int i = 0
        while i < words.length {
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
