// deque_test.ls — std.core.deque (growable ring buffer) correctness +
// ownership/memcheck probe. Prints "ok"/"FAIL", then "DEQUE PASS".

import std.core.deque
import std.core.vec
import std.core.str

def check(bool cond, Str label) {
    if cond { @print(f"ok {label}") } else { @print(f"FAIL {label}") }
}

def main() {
    // ---- empty / both ends ----
    Deque(int) d = {}
    check(d.empty?, "empty init")
    check(d.len() == 0, "len 0")

    d.push_back(2)      // [2]
    d.push_back(3)      // [2,3]
    d.push_front(1)     // [1,2,3]
    d.push_front(0)     // [0,1,2,3]
    check(d.len() == 4, "len 4")
    check(d.front().unwrap() == 0, "front 0")
    check(d.back().unwrap() == 3, "back 3")
    check(d.get(2).unwrap() == 2, "get(2) = 2")

    check(d.pop_front().unwrap() == 0, "pop_front 0")
    check(d.pop_back().unwrap() == 3, "pop_back 3")
    check(d.len() == 2, "len 2 after pops")     // [1,2]
    check(d.front().unwrap() == 1 && d.back().unwrap() == 2, "front 1 back 2")

    // ---- growth + wraparound stress ----
    Deque(int) w = {}
    for (int i = 0; i < 100; i = i + 1) { w.push_back(i) }     // forces several grows
    for (int i = 0; i < 50; i = i + 1) { w.push_front(0 - 1 - i) }  // -1..-50 at front
    check(w.len() == 150, "grow/wrap len 150")
    check(w.front().unwrap() == 0 - 50, "front -50")
    check(w.back().unwrap() == 99, "back 99")
    check(w.get(50).unwrap() == 0, "logical index 50 = 0")
    // drain front: -50, -49, ... should be ascending
    int prev = w.pop_front().unwrap()
    bool ok = true
    while w.empty? == false {
        int cur = w.pop_front().unwrap()
        if cur != prev + 1 { ok = false }
        prev = cur
    }
    check(ok, "drained front in ascending order")
    check(prev == 99, "last drained 99")

    // ---- literal + to_vec + for-in ----
    Deque(int) lit = [10, 20, 30]
    check(lit.len() == 3, "literal len 3")
    Vec(int) v = lit.to_vec()
    check(v.len() == 3 && v[0] == 10 && v[2] == 30, "to_vec front->back")
    int sum = 0
    for x in lit { sum = sum + x }
    check(sum == 60, "for-in sum 60")

    // ---- sliding window of last K (the signal-processing pattern) ----
    Deque(int) win = {}
    Vec(int) stream = [5, 3, 8, 1, 9, 2, 7]
    int k = 3
    int last_sum = 0
    for s in stream {
        win.push_back(s)
        if win.len() > k {
            win.pop_front()            // evict oldest, keep window size K
        }
    }
    for x in win { last_sum = last_sum + x }
    check(win.len() == 3, "window size capped at 3")
    check(last_sum == 18, "window {1,2,7} sum 18")   // last 3 of stream

    // ---- Deque(Str): has_drop (memcheck probe) ----
    Deque(Str) sd = {}
    sd.push_back("beta")
    sd.push_front("alpha")
    sd.push_back("gamma")        // [alpha, beta, gamma]
    Str f = sd.front().unwrap()
    check(f.eq?("alpha"), "Str front alpha")
    Str popped = sd.pop_back().unwrap()
    check(popped.eq?("gamma"), "Str pop_back gamma")
    check(sd.len() == 2, "Str len 2")
    // sd still owns "alpha","beta" → dropped on scope exit (memcheck probe).

    @print("DEQUE PASS")
}
