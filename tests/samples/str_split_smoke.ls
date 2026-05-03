/* Smoke test: split + join. */
fn main() -> int {
    string s = "a,b,c,d"
    vec(string) parts = s.split(",")
    print(parts.length)
    for i in 0..parts.length {
        print(parts[i])
    }
    string back = ",".join(parts)
    print(back)
    return 0
}
