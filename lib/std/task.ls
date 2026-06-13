// std/task.ls — structured concurrency.
//
//   Task t = Task.new(|| ...body... )   // run body on an OS worker thread
//   int r = t.join()                     // wait, return the body's result
//
// (`Task.new() { || ... }` trailing-block form also works; a bare-block
//  `Task.new { ... }` fights LS's struct-literal grammar, so `(|| ...)` is the
//  idiom.)
//
// The body MOVE-captures whatever it touches, so each Task is ISOLATED — no
// shared mutable state — which makes it sound WITHOUT a lifetime system: the
// worker owns its captures and drops them exactly once; the spawning scope has
// already marked those sources MOVED (use-after-move is a compile error).
//
// `Thread` (raw, you-manage-sharing) is reserved for a later low-level tier.
// Shared state (Mutex / Atomic) and a generic `Task(T)` result type come next;
// v1 bodies return `int`.

struct Task { object h }

impl Task {
    // Spawn `f` on a worker. Trailing-closure sugar gives `Task.new { ... }`.
    static fn new(Block()->int f) -> Task {
        return Task{ h: __task_spawn(f) }
    }

    // Wait for the worker and return its result. Terminal — call once.
    fn join(&self) -> int {
        return __task_join(self.h)
    }
}
