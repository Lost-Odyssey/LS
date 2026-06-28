/* BF-042 / BF-043 regression: global Vec with a list literal initializer.
   VR-LIM-016 (fixed F1): global `Vec(T) v = [...]` now monomorphizes
   `Vec(T).__from_list` via forward-declare from the pending-generic queue. */

import std.core.vec
import std.core.str

struct Tag { Str label }

Vec(int)    nums  = [1, 2, 3]                                   // global list literal
Vec(Str)    words = ["foo".upper(), "bar".upper()]             // owned-element literal
Vec(Tag)    tags  = [Tag { label: "x".upper() }, Tag { label: "y".upper() }]  // has_drop struct

def main() -> int {
    int s = nums[0] + nums[1] + nums[2]
    @print(f"sum={s}")

    /* mutate after literal init — must still behave like a normal Vec */
    nums.push(4)
    int s2 = s + nums[3]
    @print(f"sum2={s2}")

    /* owned-element Vecs read back correctly */
    @print(f"words={words[0]}{words[1]}")
    @print(f"tags={tags[0].label}{tags[1].label}")

    /* Bind has_drop struct fields into locals before comparing (Phase H deep
       copy). Note: putting `tags[i].label` directly inside a short-circuit `&&`
       trips a SEPARATE pre-existing codegen bug — has_drop vec-element field
       access in a `&&` condition → temp_drop alloca dominance failure — which is
       unrelated to global-vec init/cleanup. See bugfix_registry (pending). */
    Str t0 = tags[0].label
    Str t1 = tags[1].label
    bool tags_ok = t0.eq?("X") && t1.eq?("Y")

    if s == 6 && s2 == 10 && words[0].eq?("FOO") && words[1].eq?("BAR") && tags_ok {
        @print("GLOBAL_VEC_LIT PASS")
    }
    return 0
}
