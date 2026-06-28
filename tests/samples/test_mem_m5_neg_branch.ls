// M-5 负向：仅一条分支 move（MAYBE_MOVED），分支后使用源 → 编译期拒绝。
// 验证 checker 的分支快照合并：LIVE∧MOVED → MAYBE_MOVED = 死亡。
// 期望：[move error] ... use of maybe-moved variable 's'，rc != 0。
def main(int argc) -> int {
    Str s = "hello".upper()
    if argc > 0 {
        Str b = s   // 仅 then 分支 move s → s 变 MAYBE_MOVED
    }
    @print(s)        // ← s 可能已 move，编译错误
    return 0
}
