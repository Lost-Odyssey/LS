// arena_pod_reject.ls — negative: Arena element type must be Pod.
// `Arena(Str).alloc(...)` must be a COMPILE error (Str owns a malloc buffer →
// bulk reset would leak it). See docs/plan_arena_allocator.md §4.3.
// Expected diagnostic: "requires T: Pod, but 'Str' does not implement Pod".

import std.mem.arena
import std.core.str

def main() -> int {
    Arena(Str) a = {}
    Str s = "hello"
    int h = a.alloc(s)        // <-- compile error here
    @print(f"unreachable {h}")
    return 0
}
