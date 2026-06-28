// Phase 0 smoke: the new condition-variable intrinsics resolve and run.
// Single-threaded — exercises the non-blocking ops (init/signal/broadcast/
// destroy) which are well-defined no-ops with no waiters. __cond_wait is NOT
// called here (it would block forever with no signaller); it gets exercised in
// the blocking Chan tests (Phase 2) where a peer thread signals.

def main() -> int {
    object cv  = __cond_init()
    object mtx = __mutex_init()

    __cond_signal(cv)       // no waiters -> no-op
    __cond_broadcast(cv)    // no waiters -> no-op

    __cond_destroy(cv)
    __mutex_destroy(mtx)

    @print("cond smoke ok")
    return 0
}
