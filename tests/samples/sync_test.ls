// std.sync.lock — single-threaded correctness of the bare Mutex and SpinLock:
// lock!/unlock!/trylock! bracket a critical section over data you pair with the
// lock yourself; Mutex.__drop destroys the OS lock. Runs under --memcheck (must
// be 0/0/0). Cross-thread mutual exclusion is in sync_thread_test.ls.

import std.core.vec
import std.sync.lock

def main() {
    // ---- Mutex bracketing a paired Vec ----
    Mutex m = {}
    m.init()
    Vec(int) data = []
    m.lock!()
    data.push(1) data.push(2) data.push(3) data.push(4)
    m.unlock!()

    m.lock!()
    int total = 0
    for i in 0..data.len() { total = total + data.get!(i) }
    int len = data.len()
    m.unlock!()
    if total != 10 { @print("SYNC FAIL mutex total") return }
    if len != 4 { @print("SYNC FAIL mutex len") return }

    // trylock on a free lock succeeds; release it. A second trylock while held
    // would fail — but that's the same thread, so we don't deadlock-test here.
    if !m.trylock!() { @print("SYNC FAIL mutex trylock") return }
    m.unlock!()

    // ---- SpinLock bracketing a paired Vec ----
    SpinLock s = {}
    Vec(int) sd = []
    s.lock!()
    sd.push(10) sd.push(20)
    s.unlock!()

    s.lock!()
    int sum = 0
    for i in 0..sd.len() { sum = sum + sd.get!(i) }
    s.unlock!()
    if sum != 30 { @print("SYNC FAIL spin sum") return }

    if !s.trylock!() { @print("SYNC FAIL spin trylock") return }
    s.unlock!()

    @print("SYNC OK")
    // m drops here → __mutex_destroy frees the OS lock. data/sd are plain Vecs,
    // auto-dropped. SpinLock has no handle. memcheck must be 0/0/0.
}
