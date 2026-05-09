// Phase C.7: by-move captures of vec(T) / map(K,V) / struct(has_drop).
// Validates:
//   - Capturing an owned vec(int) and reading via .length / [i]
//   - Capturing a vec(string) (heap-of-heap) — env_drop frees elements + buffer
//   - Capturing a map(string, int) — uses __ls_map_XX_drop helper
//   - Capturing a struct that owns a string field — uses Struct.__drop
//   - All four cases survive AOT + JIT and report 0 leaks under memcheck

type IntPicker = Block(int) -> int
type Joiner    = Block(string) -> string
type Lookup    = Block(string) -> int
type Stamper   = Block(string) -> string

struct Tag {
    string name
    int weight
}

fn make_picker(vec(int) v) -> IntPicker {
    return |i| v[i]
}

fn make_joiner(vec(string) parts) -> Joiner {
    return |sep| {
        // Walk the captured vec and concatenate with sep.
        string out = ""
        int n = parts.length
        int i = 0
        while i < n {
            if i > 0 { out = out + sep }
            out = out + parts[i]
            i = i + 1
        }
        return out
    }
}

fn make_lookup(map(string, int) scores) -> Lookup {
    return |key| {
        if scores.contains_key(key) {
            return scores.get(key)
        }
        return 0
    }
}

fn make_stamper(Tag t) -> Stamper {
    return |sep| t.name + sep + to_string(t.weight)
}

fn main() {
    // 1) vec(int) capture
    vec(int) nums = [10, 20, 30, 40]
    IntPicker pick = make_picker(nums)
    print(pick(0))                       // 10
    print(pick(2))                       // 30
    print(pick(3))                       // 40

    // 2) vec(string) capture — string elements freed via env_drop
    vec(string) parts = ["a".upper(), "b".upper(), "c".upper()]
    Joiner j = make_joiner(parts)
    print(j("-"))                        // A-B-C

    // 3) map(string, int) capture
    map(string, int) scores = { "alice" -> 90, "bob" -> 75 }
    Lookup look = make_lookup(scores)
    print(look("alice"))                 // 90
    print(look("bob"))                   // 75
    print(look("nobody"))                // 0   (default)

    // 4) struct(has_drop) capture
    Tag t = Tag { name: "lvl".upper(), weight: 7 }
    Stamper st = make_stamper(t)
    print(st(":"))                       // LVL:7
}
