import mod_a

fn main() -> int {
    int v = mod_a.get()
    print(f"counter={v}")
    mod_a.set(42)
    int v2 = mod_a.get()
    print(f"after_set={v2}")
    if v == 100 && v2 == 42 {
        print("MODVAR_BASIC PASS")
    }
    return 0
}
