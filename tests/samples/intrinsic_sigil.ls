// @-sigil intrinsics compile & run identically to the legacy __ spellings.
import std.core.vec

def main() -> int {
    Vec(int) v = [10, 20, 30]
    int last = @take(v.data[2])     // move out of a raw slot (POD: bit-read)
    @print(f"last={last}")
    Str a = f"hi"
    Str b = @move(a)                // tracked variable move
    @print(b)
    return 0
}
