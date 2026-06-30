// std/heap.ls — pure-LS binary MAX-heap priority queue, built on Vec(T).
//
// A complete binary tree stored in a Vec(T) (parent i → children 2i+1, 2i+2),
// kept in max-heap order: every parent compares >= its children (by Order's `<`).
// peek/pop return the LARGEST element. This is the trait-bound container probe
// from docs/plan_std_containers.md — a generic container whose methods call a
// `where T: Order` operator (`<`) on the element type under monomorphization.
//
// MAX-heap (matches Rust's BinaryHeap / C++ priority_queue). For a MIN-heap
// (smallest-first, e.g. Dijkstra) either negate keys or wrap them in a type whose
// Order is reversed.
//
// Ownership: push moves T in; pop moves the max OUT (no clone) via Vec.pop; peek
// returns a CLONE of the max. Sift up/down MOVE elements with Vec.swap (__take,
// no clone) and compare them in place through the raw `data` pointer (borrow, no
// per-compare clone). The heap drops its Vec(T) on scope exit (auto-derived
// Destroy), which drops every remaining element.

import std.core.vec
import std.core.num

struct BinaryHeap(T) { Vec(T) data }

// Returns an empty, owned max-heap. The empty Vec is bound through an explicit
// `Vec(T)` local so the generic element type is resolved (mirrors new_stack).
def new_heap(T)() -> BinaryHeap(T) {
    Vec(T) d = {}
    return BinaryHeap(T) { data: d }
}

methods(T) BinaryHeap(T) {
    // ---- queries ----

    def len(&self) -> int { return self.data.len() }
    def empty?(&self) -> bool { return self.data.empty? }

    // Clone of the largest element, or None if empty.
    def peek(&self) -> Option(T) where T: Order { return self.data.get(0) }

    // ---- mutation ----

    // Insert x and restore the heap order by sifting it up toward the root while
    // it is larger than its parent. Compares through the raw data pointer (borrow,
    // no clone); swaps move elements (Vec.swap = __take, no clone).
    def push(&!self, T x) where T: Order {
        self.data.push(x)
        int i = self.data.len() - 1
        while i > 0 {
            int parent = (i - 1) / 2
            if self.data.data[parent] < self.data.data[i] {
                self.data.swap(parent, i)
                i = parent
            } else {
                break
            }
        }
    }

    // Remove and return the largest element (moved out — no clone), or None if
    // empty. Swaps the max to the end, pops it, then sifts the new root down past
    // any larger child.
    def pop(&!self) -> Option(T) where T: Order {
        int n = self.data.len()
        if n == 0 { return None }
        self.data.swap(0, n - 1)          // max → last slot (swap guards i==j)
        Option(T) out = self.data.pop()   // remove & return the old root
        int sz = self.data.len()
        int i = 0
        while true {
            int l = 2 * i + 1
            int r = 2 * i + 2
            int largest = i
            if l < sz {
                if self.data.data[largest] < self.data.data[l] { largest = l }
            }
            if r < sz {
                if self.data.data[largest] < self.data.data[r] { largest = r }
            }
            if largest == i { break }
            self.data.swap(i, largest)
            i = largest
        }
        return out
    }

    // Drop every element, keep the buffer (cheap reuse).
    def clear(&!self) { self.data.clear() }

    // Heap-literal `BinaryHeap(T) h = [a, b, c]` opt-in (reserved-method protocol,
    // like Vec.__from_list): each element is pushed (heapified) as it arrives.
    def __from_list(&!self, T x) where T: Order { self.push(x) }
}
