/* BF-042 regression: global POD vec with a list literal initializer.
   Pre-fix the global vec struct {data,len,cap} stayed zeroed (the generic
   global-init path stored an array aggregate, not a heap vec) → reads returned
   empty/0. Now emit_global_var_init builds the vec in place via push. */

vec(int) nums = [1, 2, 3]

fn main() -> int {
    int s = nums[0] + nums[1] + nums[2]
    print(f"sum={s}")

    /* mutate after literal init — must still behave like a normal vec */
    nums.push(4)
    int s2 = s + nums[3]
    print(f"sum2={s2}")

    if s == 6 && s2 == 10 {
        print("GLOBAL_VEC_LIT PASS")
    }
    return 0
}
