// std/stack.ls — Generic LIFO stack, pure LS, built on std.core.vec's Vec(T).
//
// The first generic container (see docs/plan_std_containers.md). Doubles as a
// probe for two generic paths:
//   1. generic struct with a Vec(T) field  → monomorphized auto-drop / clone
//   2. generic impl with &!self mutating methods
//
// Ownership:
//   push   moves T into the stack.
//   pop    moves T out of the stack (no clone) and returns it.
//   peek   returns a clone of the top element (has_drop T is deep-copied).
//   clear  drops every element; the stack itself drops its Vec(T) on scope exit,
//          which in turn drops each remaining element.

import std.core.vec

struct Stack(T) {
    Vec(T) data
}

// ---- new_stack(T)() -> Stack(T) ----
// Returns an empty, owned stack. The empty Vec literal is bound through an
// explicit `Vec(T)` local so the generic element type is resolved.

def new_stack(T)() -> Stack(T) {
    Vec(T) d = {}
    return Stack(T) { data: d }
}

methods(T) Stack(T) {
    // Move x onto the top of the stack.
    def push(&!self, T x) {
        self.data.push(x)
    }

    // Remove the top element and return it (moved out — no clone). Callers must
    // ensure the stack is non-empty (mirrors the original contract); the None
    // arm is unreachable in well-formed use.
    def pop(&!self) -> T {
        match self.data.pop() {
            Some(x) => { return x }
            None => { return self.data[0] }
        }
    }

    // Return a clone of the top element without removing it.
    def peek(&self) -> T {
        match self.data.last() {
            Some(x) => { return x }
            None => { return self.data[0] }
        }
    }

    // Number of elements.
    def len(&self) -> int {
        return self.data.len()
    }

    // True when the stack holds no elements.
    def is_empty(&self) -> bool {
        return self.data.empty?
    }

    // Drop every element, leaving an empty stack.
    def clear(&!self) {
        self.data.clear()
    }
}
