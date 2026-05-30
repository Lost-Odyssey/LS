import mod_a

fn main() -> int {
    string g = mod_a.get_greeting()
    print(f"greeting={g}")
    if g == "hello" {
        print("MODVAR_HASDROP PASS")
    }
    return 0
}
