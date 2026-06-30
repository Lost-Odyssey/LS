// std/task.ls — structured concurrency, GENERIC over the result type.
//
//   import std.sync.task
//   Task(int) t = {}                  // construct an (unstarted) Task(T)
//   t.run(|| 6 * 7)                    // run the body on an OS worker thread
//   int r = t.join()                   // wait, MOVE the result back
//
// A Task(T) is a two-way MOVE channel: the closure move-captures its inputs
// (isolation — no shared mutable state) and the result moves out. Both ends
// reuse LS's existing ownership machinery, so this is sound WITHOUT a lifetime
// system, and correct for has_drop results (Vec/Str/struct are moved, not
// shallow-copied). The worker owns its captures and drops them exactly once;
// the spawning scope has already marked those sources MOVED (use-after-move is
// a compile error).
//
// Mechanism (docs/plan_task_generic.md §2): codegen synthesises a per-T thunk
// `*box = closure(env)` that stores the closure's by-value result into a `*T`
// box the Task owns; the runtime only runs threads and never touches the result
// bytes. join() moves the result out of the box via __take, then frees the box
// slot (not the T heap inside it).
//
// `run` is a plain instance method on Task(T), so the closure's expected return
// type T is plumbed from the impl-level type param (no method-level generic or
// generic static method needed — the construct-then-run shape sidesteps those
// half-built foundations). `Thread` (raw, you-manage-sharing) is reserved for a
// later low-level tier; shared state (Mutex / Atomic) comes next.
//
// Task(T) is NOT has_drop (its fields are POD pointers), so it has no automatic
// drop: a Task you `run()` MUST be `join()`ed exactly once (an un-joined running
// Task leaks its box + thread; a never-run Task is inert and leaks nothing).

struct Task(T) { object h; *T box }

methods Task(T) {
    // Start: run `f` on a worker thread. The result will be written into a fresh
    // *T box this Task owns. Construct the Task first (`Task(int) t = {}`), call
    // run, then join. `f` move-captures its inputs (those sources are MOVED).
    def run(&!self, Block()->T f) {
        self.box = std.sys.c.malloc(sizeof(T)) as *T
        self.h = __task_spawn(f, self.box)
    }

    // Wait for the worker and MOVE its result back. Terminal — call once.
    def join(&self) -> T {
        __task_join(self.h)
        T r = __take(self.box[0])        // move the result out of the box
        std.sys.c.free(self.box as *u8)      // free the slot (not the T heap)
        return r
    }
}
