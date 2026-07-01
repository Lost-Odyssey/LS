import std.core.vec
def main() -> int {
    Vec(int) v = [7, 8, 9]         // requires the FromList protocol on Vec
    @print(f"len={v.len()} v0={v.get!(0)}")
    return 0
}
