import mod_a
import mod_b

def main() -> int {
    int a = mod_a.get()
    int b = mod_b.get()
    @print(f"a={a} b={b}")
    if a == 100 && b == 200 {
        @print("MODVAR_CROSS PASS")
    }
    return 0
}
