// std/set.ls — pure-LS hash set, built on std.core.map's Map(T, bool).
//
// See docs/plan_std_set.md. v1 wraps Map(T, bool): the element is the map key,
// the value is a dummy `true`. This reuses Map's whole machinery (Robin Hood
// open addressing, Fibonacci scatter, has_drop ownership, grow/clone/destroy)
// at the cost of one wasted bool per slot — acceptable for v1 (a vals-free
// specialization is Phase 5, deferred until profiling demands it).
//
// Ownership:
//   insert   moves T into the set (the inner map owns it).
//   has?     borrows T for the lookup (no move, no clone).
//   remove   drops the stored T in place.
//   the set drops its Map(T, bool) on scope exit (auto-derived Destroy),
//   which drops every remaining element.

import std.core.map
import std.core.hash
import std.core.vec

struct Set(T) { Map(T, bool) m }

// Borrowing iterator over a Set(T). Holds RAW pointers into the inner map's
// buffers (it does NOT own them), so it is non-has_drop and must not outlive
// the set, nor may the set be mutated during iteration (rehash/backward-shift
// relocate slots). Produced by Set.iter(); driven by `for x in s`. Mirrors
// MapIter, but yields just the element key (a clone), not an Entry.
struct SetIter(T) { *u8 ctrl; *T keys; int cap; int i }

// ---- new_set(T)() -> Set(T) ----
// Returns an empty, owned set. The empty Map literal is bound through an
// explicit `Map(T, bool)` local so the generic element type is resolved
// (mirrors std.core.stack's new_stack).
def new_set(T)() -> Set(T) {
    Map(T, bool) inner = {}
    return Set(T) { m: inner }
}

methods(T) Set(T) {
    // ---- queries ----

    // Number of distinct elements.
    def len(&self) -> int { return self.m.len() }

    // True when the set holds no elements.
    def empty?(&self) -> bool { return self.m.empty? }

    // Membership test (borrows x — no move, no clone).
    def has?(&self, &T x) -> bool where T: Hash + Equal {
        return self.m.has?(x)
    }

    // Ergonomic alias of has? (reads naturally as `s.contains(x)`).
    def contains(&self, &T x) -> bool where T: Hash + Equal {
        return self.m.has?(x)
    }

    // ---- mutation ----

    // Move x into the set. Returns true if x was newly added, false if it was
    // already present (in which case the new x overwrites the old — same value,
    // so no observable difference for a set; the old dummy is dropped by set()).
    def insert(&!self, T x) -> bool where T: Hash + Equal {
        bool existed = self.m.has?(x)   // read-only borrow of x (auto)
        self.m.set(x, true)             // then move x into the table
        return existed == false
    }

    // Remove x. Returns true if x was present (and is now dropped), false if
    // x was absent.
    def remove(&!self, &T x) -> bool where T: Hash + Equal {
        match self.m.remove(x) {
            Some(_) => { return true }
            None => { return false }
        }
    }

    // Drop every element, keep the buffer (cheap reuse).
    def clear(&!self) { self.m.clear() }

    // ---- literal protocol ----

    // Set-literal `Set(T) s = [a, b, c]` opt-in (reserved-method protocol, like
    // Vec.__from_list): the checker lowers a `[..]` list literal into `Set s = {}`
    // plus one __from_list call per element (each moved in). Duplicate elements
    // collapse — set() updates the existing slot, len does not grow → the literal
    // de-duplicates for free.
    def __from_list(&!self, T x) where T: Hash + Equal { self.m.set(x, true) }

    // ---- iteration / export ----

    // Borrowing iterator over the elements (slot order, NOT insertion order).
    // `for x in s` desugars to driving this via the Iterator(T) protocol.
    def iter(&self) -> SetIter(T) {
        return SetIter(T){ ctrl: self.m.ctrl, keys: self.m.keys,
                           cap: self.m.cap, i: 0 }
    }

    // Collect clones of all elements into a Vec(T) (slot order). Reuses the
    // inner map's keys() collector.
    def to_vec(&self) -> Vec(T) { return self.m.keys() }

    // ---- bulk mutation ----

    // Add every element of `other` into self (clones — other is unchanged).
    def extend(&!self, &Set(T) other) where T: Hash + Equal {
        for (int i = 0; i < other.m.cap; i = i + 1) {
            int c = other.m.ctrl[i] as int
            if c != 255 {
                T k = other.m.keys[i]
                self.m.set(k, true)
            }
        }
    }

    // ---- set algebra (all return a fresh Set; self/other unchanged) ----

    // Elements in self OR other.
    def union(&self, &Set(T) other) -> Set(T) where T: Hash + Equal {
        Set(T) out = new_set(T)()
        out.extend(self)
        out.extend(other)
        return out
    }

    // Elements in self AND other.
    def intersect(&self, &Set(T) other) -> Set(T) where T: Hash + Equal {
        Set(T) out = new_set(T)()
        for (int i = 0; i < self.m.cap; i = i + 1) {
            int c = self.m.ctrl[i] as int
            if c != 255 {
                if other.m.has?(self.m.keys[i]) {
                    T k = self.m.keys[i]
                    out.m.set(k, true)
                }
            }
        }
        return out
    }

    // Elements in self but NOT in other.
    def difference(&self, &Set(T) other) -> Set(T) where T: Hash + Equal {
        Set(T) out = new_set(T)()
        for (int i = 0; i < self.m.cap; i = i + 1) {
            int c = self.m.ctrl[i] as int
            if c != 255 {
                if other.m.has?(self.m.keys[i]) == false {
                    T k = self.m.keys[i]
                    out.m.set(k, true)
                }
            }
        }
        return out
    }

    // ---- set predicates ----

    // True if every element of self is in other.
    def is_subset(&self, &Set(T) other) -> bool where T: Hash + Equal {
        for (int i = 0; i < self.m.cap; i = i + 1) {
            int c = self.m.ctrl[i] as int
            if c != 255 {
                if other.m.has?(self.m.keys[i]) == false { return false }
            }
        }
        return true
    }

    // True if every element of other is in self.
    def is_superset(&self, &Set(T) other) -> bool where T: Hash + Equal {
        for (int i = 0; i < other.m.cap; i = i + 1) {
            int c = other.m.ctrl[i] as int
            if c != 255 {
                if self.m.has?(other.m.keys[i]) == false { return false }
            }
        }
        return true
    }

    // True if self and other share no element.
    def is_disjoint(&self, &Set(T) other) -> bool where T: Hash + Equal {
        for (int i = 0; i < self.m.cap; i = i + 1) {
            int c = self.m.ctrl[i] as int
            if c != 255 {
                if other.m.has?(self.m.keys[i]) { return false }
            }
        }
        return true
    }
}

// ---- operator sugar ----
// `a + b` = union, `a - b` = difference. (`|`/`&` are not overloadable in LS,
// so intersection stays the named `intersect`.)

methods(T) Set(T): Add {
    def +(&self, &Set(T) rhs) -> Set(T) where T: Hash + Equal {
        return self.union(rhs)
    }
}

methods(T) Set(T): Sub {
    def -(&self, &Set(T) rhs) -> Set(T) where T: Hash + Equal {
        return self.difference(rhs)
    }
}

methods(T) SetIter(T) {
    // Iterator protocol: yield the next live element (clone-on-read, matching
    // Map.get), or None when exhausted.
    def next(&!self) -> Option(T) {
        while self.i < self.cap {
            int j = self.i
            self.i = self.i + 1
            int c = self.ctrl[j] as int
            if c != 255 {
                T k = self.keys[j]
                return Some(k)
            }
        }
        return None
    }
}
