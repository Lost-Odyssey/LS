// heap_test.ls — std.core.heap (BinaryHeap, trait-bound container) correctness
// + ownership/memcheck probe. Prints "ok <label>" / "FAIL <label>", then "HEAP PASS".

import std.core.heap
import std.core.vec
import std.core.str

def check(bool cond, Str label) {
    if cond { @print(f"ok {label}") } else { @print(f"FAIL {label}") }
}

def main() {
    // ---- BinaryHeap(int): empty / push / peek ----
    BinaryHeap(int) h = new_heap(int)()
    check(h.empty?, "int empty init")
    check(h.len() == 0, "int len 0")

    h.push(3)
    h.push(1)
    h.push(4)
    h.push(1)
    h.push(5)
    h.push(9)
    h.push(2)
    check(h.len() == 7, "int len 7")
    check(h.peek().unwrap() == 9, "int peek max 9")
    check(h.len() == 7, "int peek non-destructive")

    // pop drains in DESCENDING order (max-heap)
    check(h.pop().unwrap() == 9, "pop 9")
    check(h.pop().unwrap() == 5, "pop 5")
    check(h.pop().unwrap() == 4, "pop 4")
    check(h.pop().unwrap() == 3, "pop 3")
    check(h.pop().unwrap() == 2, "pop 2")
    check(h.pop().unwrap() == 1, "pop 1")
    check(h.pop().unwrap() == 1, "pop 1 (dup)")
    check(h.len() == 0, "drained empty")
    match h.pop() { Some(_) => { check(false, "pop empty None") } None => { check(true, "pop empty None") } }

    // ---- literal + heapsort round-trip ----
    BinaryHeap(int) lit = [7, 2, 8, 1, 9, 3]
    check(lit.len() == 6, "literal len 6")
    Vec(int) desc = {}
    while lit.empty? == false {
        desc.push(lit.pop().unwrap())
    }
    check(desc.len() == 6, "drained 6")
    check(desc.is_sorted() == false, "descending, not ascending")
    check(desc[0] == 9 && desc[5] == 1, "descending 9..1")

    // ---- BinaryHeap(Str): has_drop element (memcheck probe) ----
    BinaryHeap(Str) sh = new_heap(Str)()
    sh.push("banana")
    sh.push("apple")
    sh.push("cherry")
    // lexicographic max = "cherry"
    Str top = sh.peek().unwrap()
    check(top.eq?("cherry"), "Str peek max cherry")
    Str p1 = sh.pop().unwrap()
    check(p1.eq?("cherry"), "Str pop cherry")
    check(sh.len() == 2, "Str len 2 after pop")
    // sh still owns "banana" + "apple"; dropped on scope exit (memcheck probe).

    @print("HEAP PASS")
}
