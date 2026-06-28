// guard_priv_reject.ls — touching guarded data without the lock is a compile
// error: the value field is private, reachable only through lock/get.
import std.core.vec
import std.sync.lock

def main() -> int {
    Guard(Vec(int)) data = {}
    data.init()
    int n = data.value.len()    // ✗ 'value' is private — must go through lock/get
    @print(n)
    return 0
}
