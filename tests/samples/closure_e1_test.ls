// Phase E.1: closure body captured Vec passed to fn — clone fallback.
// Vec is by-move capture (unlike builtin vec by-ref). The closure's env
// owns its copy; the outer variable is moved after capture.
//
// When the closure body passes the captured vec to a by-value function,
// it must .copy() to avoid moving the closure's internal storage.

import std.vec
import std.map
import std.str

type Adder      = Block() -> int
type MapCounter = Block() -> int

fn sum_int_vec(Vec(int) v) -> int {
    int s = 0
    int i = 0
    while i < v.len() {
        s = s + v[i]
        i = i + 1
    }
    return s
}

fn vec_len(Vec(int) v) -> int {
    return v.len()
}

fn count_keys(&Map(Str, int) m) -> int {
    return m.len()
}

fn main() {
    // E.1.1: closure captures Vec(int) by-move, body clones before passing.
    Vec(int) nums = [10, 20, 30]
    Adder adder = || {
        return sum_int_vec(nums.copy())
    }
    int r1 = adder()
    print(r1)               // 60
    // With by-move capture, `nums` is moved into the closure.
    // The closure holds its own copy and clones it on each call.

    // E.1.2: closure captures Vec(int), body returns length via clone
    Vec(int) items = [1, 2, 3, 4, 5]
    Adder counter = || {
        return vec_len(items.copy())
    }
    int r2 = counter()
    print(r2)               // 5

    // E.1.3: multiple calls — closure clones its internal copy each time
    Vec(int) data = [7, 8, 9]
    Adder summer = || {
        return sum_int_vec(data.copy())
    }
    print(summer())         // 24
    print(summer())         // 24

    // E.1.4: closure captures Map(Str,int) by-move, body borrows for read
    Map(Str, int) scores = {}
    scores.set("alice", 42)
    scores.set("bob", 99)
    MapCounter key_counter = || {
        return count_keys(scores)
    }
    print(key_counter())    // 2
    print(key_counter())    // 2
}
