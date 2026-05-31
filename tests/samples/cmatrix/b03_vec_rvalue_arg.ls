// f(v.get(i)) nested-vec rvalue arg (E)
fn take(vec(string) v) -> int { return v.length }
fn main() {
    vec(vec(string)) m = []
    vec(string) r = ["a","b"]; m.push(r)
    print(take(m.get(0)))
}
