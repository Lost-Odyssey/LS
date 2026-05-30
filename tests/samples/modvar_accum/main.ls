import mod_counter

fn main() -> int {
    mod_counter.inc()
    mod_counter.inc()
    mod_counter.inc()
    int v = mod_counter.get()
    print(f"total={v}")
    mod_counter.reset()
    int v2 = mod_counter.get()
    print(f"after_reset={v2}")
    if v == 3 && v2 == 0 {
        print("MODVAR_ACCUM PASS")
    }
    return 0
}
