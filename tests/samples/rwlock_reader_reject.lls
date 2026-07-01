// rwlock_reader_reject.ls — a reader cannot mutate the shared data: the read()
// closure receives a read-only &T, so calling a &!self method (push) on it is a
// compile error. This is RwLock's core guarantee (readers run in parallel
// precisely because none of them can write).
import std.core.vec
import std.sync.lock

def main() -> int {
    RwLock(Vec(int)) data = {}
    data.init()
    data.write(|v| { v.push(1) })
    // ✗ v is &Vec(int) (read-only); push needs &!self.
    int n = data.read(int)(|v| { v.push(99) return v.len() })
    @print(n)
    return 0
}
