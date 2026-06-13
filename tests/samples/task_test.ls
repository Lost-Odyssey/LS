// std.task — structured concurrency, end to end. Validates the core model:
// MOVE-capturing owned heap (Vec) into a worker is single-owner and sound (no
// auto-drop double-free), and the result comes back.
//
//   Task t = Task.new(|| body)   // run body on an OS worker; body MOVE-captures
//   int r = t.join()             // wait, return body's result
//
// (`Task.new(|| ...)` is the idiom; `Task.new() { || ... }` trailing-block form
// also works. A bare-block `Task.new { ... }` fights LS's struct-literal grammar.)

import std.vec
import std.task

fn part_sum(Vec(int) v) -> int {
    int s = 0
    for x in v { s = s + x }
    return s
}

fn build(int base, int n) -> Vec(int) {
    Vec(int) v = []
    for (int i = 0; i < n; i = i + 1) { v.push(base + i) }
    return v
}

fn main() {
    // Four owned vectors, each MOVE-captured into a worker. After spawn, main no
    // longer owns a/b/c/d (marked MOVED) — only the workers drop them, once.
    Vec(int) a = build(0, 1000)       // 0..999
    Vec(int) b = build(1000, 1000)    // 1000..1999
    Vec(int) c = build(2000, 1000)    // 2000..2999
    Vec(int) d = build(3000, 1000)    // 3000..3999

    Task t1 = Task.new(|| part_sum(a))
    Task t2 = Task.new(|| part_sum(b))
    Task t3 = Task.new(|| part_sum(c))
    Task t4 = Task.new(|| part_sum(d))

    int total = t1.join() + t2.join() + t3.join() + t4.join()

    // sum 0..3999 = 3999 * 4000 / 2 = 7998000
    print(total)
    if total == 7998000 {
        print("TASK OK")
    } else {
        print("TASK FAIL")
    }
}
