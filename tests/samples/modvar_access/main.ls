import constants

fn main() -> int {
    int a = constants.ANSWER
    string v = constants.VERSION
    print(f"answer={a} version={v}")
    if a == 42 && v == "1.0" {
        print("MODVAR_ACCESS PASS")
    }
    return 0
}
