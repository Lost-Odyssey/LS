/* Phase F.1 tests: [move v] capture spec
   Verifies that vec/map captured with [move] transfer ownership into the
   closure env (by-move), solving the dangling-pointer factory pattern.
   Expected output (one per line):
     6
     99
     4
     hello world
*/

type Summer = Block() -> int
type ScoreFn = Block() -> int
type Counter = Block() -> int
type MsgFn = Block() -> string

/* F.1.1: vec [move] — factory pattern (correct after outer scope exits).
   Without [move], nums would be by-ref (dangling after make_summer returns). */
fn make_summer() -> Summer {
    vec(int) nums = [1, 2, 3]
    return [move nums] || {
        int s = 0
        int i = 0
        while i < nums.length {
            s = s + nums[i]
            i = i + 1
        }
        return s
    }
}

/* Pollute the stack to verify by-value capture (not dangling pointer). */
fn pollute() -> int {
    vec(int) trash = [999, 888, 777]
    return trash[0]
}

/* F.1.2: map [move] — factory pattern */
fn make_score_fn() -> ScoreFn {
    map(string, int) scores = {}
    scores.set("alice", 99)
    return [move scores] || {
        return scores.get("alice")
    }
}

/* F.1.3: vec [move] with multiple elements and computation */
fn make_counter() -> Counter {
    vec(int) items = [10, 20, 30, 40]
    return [move items] || {
        return items.length
    }
}

/* F.1.4: map [move] with string value */
fn make_msg_fn() -> MsgFn {
    map(string, string) msgs = {}
    msgs.set("greeting", "hello world")
    return [move msgs] || {
        return msgs.get("greeting")
    }
}

fn main() {
    /* F.1.1: vec [move] factory + stack-pollute test */
    Summer f = make_summer()
    int x = pollute()
    print(f())         /* 6 */

    /* F.1.2: map [move] factory */
    ScoreFn sf = make_score_fn()
    print(sf())        /* 99 */

    /* F.1.3: counter from moved vec */
    Counter cnt = make_counter()
    print(cnt())       /* 4 */

    /* F.1.4: map [move] with string value */
    MsgFn mf = make_msg_fn()
    print(mf())        /* hello world */
}
