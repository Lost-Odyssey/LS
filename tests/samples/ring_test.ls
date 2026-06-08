// ring_test.ls — std.ring (rte_ring-style fixed ring) correctness + memcheck.
// Prints "ok <label>" / "FAIL <label>" then "RING PASS".

import std.vec
import std.ring

fn check(bool c, string l) {
    if c { print(f"ok {l}") } else { print(f"FAIL {l}") }
}

fn main() {
    // ---- Ring(int): capacity rounding + basic FIFO ----
    Ring(int) r = new_ring(int)(3)        // rounds up to 4
    check(r.capacity() == 4, "cap rounds to 4")
    check(r.is_empty(), "empty init")
    check(r.len() == 0, "len 0")

    check(r.enqueue(1), "enq 1")
    check(r.enqueue(2), "enq 2")
    check(r.enqueue(3), "enq 3")
    check(r.enqueue(4), "enq 4 (fills)")
    check(r.is_full(), "full at cap 4")
    check(r.enqueue(5) == false, "enq 5 rejected when full")
    check(r.len() == 4, "len 4")
    check(r.free_space() == 0, "free 0")

    // FIFO dequeue
    match r.dequeue() { Some(v) => { check(v == 1, "deq 1") } None => { check(false, "deq 1") } }
    match r.dequeue() { Some(v) => { check(v == 2, "deq 2") } None => { check(false, "deq 2") } }
    check(r.len() == 2, "len 2 after 2 deq")

    // ---- wrap-around: enqueue past the size boundary reuses freed slots ----
    check(r.enqueue(5), "enq 5 wraps")
    check(r.enqueue(6), "enq 6 wraps")
    check(r.is_full(), "full again")
    // remaining order should be 3,4,5,6
    Vec(int) drained = r.dequeue_burst(10)
    check(drained.len() == 4, "burst drained 4")
    check(drained[0] == 3, "wrap order [0]=3")
    check(drained[1] == 4, "wrap order [1]=4")
    check(drained[2] == 5, "wrap order [2]=5")
    check(drained[3] == 6, "wrap order [3]=6")
    check(r.is_empty(), "empty after burst")
    match r.dequeue() { Some(v) => { check(false, "deq empty") } None => { check(true, "deq empty -> None") } }

    // ---- Ring(string): has_drop element ownership ----
    Ring(string) rs = new_ring(string)(2)   // cap 2
    check(rs.enqueue("alpha"), "str enq alpha")
    check(rs.enqueue("beta"), "str enq beta")
    check(rs.enqueue("gamma") == false, "str enq gamma rejected (full)")

    match rs.dequeue() { Some(s) => { check(s == "alpha", "str deq alpha") } None => { check(false, "str deq alpha") } }
    // rs still holds "beta"; enqueue "gamma" now fits (wrap)
    check(rs.enqueue("gamma"), "str enq gamma after deq")

    // clear must drop the remaining live elements (beta, gamma) exactly once
    rs.clear()
    check(rs.is_empty(), "str empty after clear")

    // refill then leave non-empty: ring drop must free remaining elements
    check(rs.enqueue("x"), "str enq x")
    check(rs.enqueue("y"), "str enq y")

    print("RING PASS")
}
