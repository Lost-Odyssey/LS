/* Phase F.1 tests: [move v] capture spec
   Verifies that Vec captured with [move] transfer ownership into the
   closure env (by-move), solving the dangling-pointer factory pattern.
   Expected output (one per line):
     6
     99
     4
     hello world
*/

import std.core.vec
import std.core.map
import std.core.str

type Summer = Block() -> int
type ScoreFn = Block() -> int
type Counter = Block() -> int
type MsgFn = Block() -> Str

/* F.1.1: Vec [move] — factory pattern (correct after outer scope exits). */
def make_summer() -> Summer {
    Vec(int) nums = [1, 2, 3]
    return [move nums] || {
        int s = 0
        int i = 0
        while i < nums.len() {
            s = s + nums[i]
            i = i + 1
        }
        return s
    }
}

/* Pollute the stack to verify by-value capture (not dangling pointer). */
def pollute() -> int {
    Vec(int) trash = [999, 888, 777]
    return trash[0]
}

/* F.1.2: map [move] — factory pattern */
def make_score_fn() -> ScoreFn {
    Map(Str, int) scores = {}
    scores.set("alice", 99)
    return [move scores] || {
        match scores.get("alice") {
            Some(v) => { return v }
            None => { return 0 }
        }
    }
}

/* F.1.3: Vec [move] with multiple elements and computation */
def make_counter() -> Counter {
    Vec(int) items = [10, 20, 30, 40]
    return [move items] || {
        return items.len()
    }
}

/* F.1.4: map [move] with Str value */
def make_msg_fn() -> MsgFn {
    Map(Str, Str) msgs = {}
    msgs.set("greeting", "hello world")
    return [move msgs] || {
        match msgs.get("greeting") {
            Some(v) => { return v }
            None => { return "" }
        }
    }
}

def main() {
    /* F.1.1: Vec [move] factory + stack-pollute test */
    Summer f = make_summer()
    int x = pollute()
    @print(f())         /* 6 */

    /* F.1.2: map [move] factory */
    ScoreFn sf = make_score_fn()
    @print(sf())        /* 99 */

    /* F.1.3: counter from moved Vec */
    Counter cnt = make_counter()
    @print(cnt())       /* 4 */

    /* F.1.4: map [move] with Str value */
    MsgFn mf = make_msg_fn()
    @print(mf())        /* hello world */
}
