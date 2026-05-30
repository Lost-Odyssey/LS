/* BF-042 / BF-043 regression: global vec with a list literal initializer.
   Pre-fix the global vec struct {data,len,cap} stayed zeroed (the generic
   global-init path stored an array aggregate, not a heap vec) → reads returned
   empty/0. Now emit_global_var_init builds the vec in place via push.
   BF-042 covers POD elements; BF-043 adds owned (string/has_drop) elements with
   per-element drop at exit (emit_global_vec_cleanup) — memcheck must stay clean. */

struct Tag { string label }

vec(int)    nums  = [1, 2, 3]                                  // POD (BF-042)
vec(string) words = ["foo".upper(), "bar".upper()]            // owned string (BF-043)
vec(Tag)    tags  = [Tag { label: "x".upper() }, Tag { label: "y".upper() }] // has_drop struct (BF-043)

fn main() -> int {
    int s = nums[0] + nums[1] + nums[2]
    print(f"sum={s}")

    /* mutate after literal init — must still behave like a normal vec */
    nums.push(4)
    int s2 = s + nums[3]
    print(f"sum2={s2}")

    /* owned-element vecs read back correctly */
    print(f"words={words[0]}{words[1]}")
    print(f"tags={tags[0].label}{tags[1].label}")

    /* Bind has_drop struct fields into locals before comparing (Phase H deep
       copy). Note: putting `tags[i].label` directly inside a short-circuit `&&`
       trips a SEPARATE pre-existing codegen bug — has_drop vec-element field
       access in a `&&` condition → temp_drop alloca dominance failure — which is
       unrelated to global-vec init/cleanup. See bugfix_registry (pending). */
    string t0 = tags[0].label
    string t1 = tags[1].label
    bool tags_ok = t0 == "X" && t1 == "Y"

    if s == 6 && s2 == 10 && words[0] == "FOO" && words[1] == "BAR" && tags_ok {
        print("GLOBAL_VEC_LIT PASS")
    }
    return 0
}
