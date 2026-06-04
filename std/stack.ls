// std/stack.ls — Generic LIFO stack, pure LS, built on vec(T).
//
// The first generic container (see docs/plan_std_containers.md). Doubles as a
// probe for two previously-untested generic paths:
//   1. generic struct with a vec(T) field  → monomorphized auto-drop / clone
//   2. generic impl with &!self mutating methods
//
// Ownership:
//   push   moves T into the stack.
//   pop    moves T out of the stack (no clone).
//   peek   returns a clone of the top element (has_drop T is deep-copied).
//   clear  drops every element; the stack itself drops its vec(T) on scope exit,
//          which in turn drops each remaining element.

struct Stack(T) {
    vec(T) data
}

// ---- new_stack(T)() -> Stack(T) ----
// Returns an empty, owned stack. The empty vec literal is bound through an
// explicit `vec(T)` local so the generic element type is resolved.

fn new_stack(T)() -> Stack(T) {
    vec(T) d = []
    return Stack(T) { data: d }
}

impl(T) Stack(T) {
    // Move x onto the top of the stack.
    fn push(&!self, T x) {
        self.data.push(x)
    }

    // Remove the top element and return it. vec.pop() drops the element rather
    // than yielding it, so we clone the top (vec.last deep-copies has_drop T)
    // and then drop the original — the caller receives an owned value. Popping
    // an empty stack returns a default/zero value (mirrors vec.last on empty).
    fn pop(&!self) -> T {
        T v = self.data.last()
        if self.data.length > 0 {
            self.data.pop()
        }
        return v
    }

    // Return a clone of the top element without removing it.
    fn peek(&self) -> T {
        return self.data.last()
    }

    // Number of elements.
    fn len(&self) -> int {
        return self.data.length
    }

    // True when the stack holds no elements.
    fn is_empty(&self) -> bool {
        return self.data.is_empty()
    }

    // Drop every element, leaving an empty stack.
    fn clear(&!self) {
        self.data.clear()
    }
}
