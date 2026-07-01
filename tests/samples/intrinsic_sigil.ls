// @-sigil intrinsics compile & run identically to the legacy __ spellings.
import std.core.vec

def main() -> int {
    Vec(int) v = [10, 20, 30]
    int copy0 = @dup(v.data[0])     // deep copy without consuming (POD: bit-copy)
    int last = @take(v.data[2])     // move out of a raw slot (POD: bit-read)
    v.truncate(2)                   // exclude the vacated slot from len
    @print(f"copy0={copy0} last={last} len={v.len()}")
    Str a = f"hi"
    Str b = @move(a)                // tracked variable move
    @print(b)
    return 0
}
