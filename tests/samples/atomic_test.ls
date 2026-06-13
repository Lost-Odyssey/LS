// std.atomic — single-threaded correctness of the lock-free scalar API.
// Every method lowers to one inline LLVM atomic instruction (SeqCst). This file
// is memcheck-clean (Atomic is POD, no heap), so it runs under --memcheck.
// Cross-thread atomicity is covered by atomic_thread_test.ls (no memcheck).

import std.atomic

fn main() {
    // ---- int: store / load ----
    Atomic(int) a = {}
    if a.get() != 0 { print("ATOMIC FAIL int zero-init") return }
    a.set(10)
    if a.get() != 10 { print("ATOMIC FAIL int set") return }

    // ---- fetch_add / fetch_sub return the PRIOR value ----
    int old1 = a.fetch_add(5)
    if old1 != 10 { print("ATOMIC FAIL add ret") return }
    if a.get() != 15 { print("ATOMIC FAIL add val") return }
    int old2 = a.fetch_sub(3)
    if old2 != 15 { print("ATOMIC FAIL sub ret") return }
    if a.get() != 12 { print("ATOMIC FAIL sub val") return }

    // ---- swap returns prior ----
    int old3 = a.swap(100)
    if old3 != 12 { print("ATOMIC FAIL swap ret") return }
    if a.get() != 100 { print("ATOMIC FAIL swap val") return }

    // ---- compare_set (CAS) ----
    if !a.compare_set(100, 7) { print("ATOMIC FAIL cas hit") return }
    if a.get() != 7 { print("ATOMIC FAIL cas val") return }
    if a.compare_set(100, 999) { print("ATOMIC FAIL cas miss") return }
    if a.get() != 7 { print("ATOMIC FAIL cas miss val") return }

    // ---- CAS loop (the spinlock building block) ----
    Atomic(int) c = {}
    c.set(0)
    for i in 0..50 {
        bool done = false
        while !done {
            int cur = c.get()
            done = c.compare_set(cur, cur + 1)
        }
    }
    if c.get() != 50 { print("ATOMIC FAIL cas loop") return }

    // ---- i64 ----
    Atomic(i64) bi = {}
    bi.set(1000000000000)
    i64 ob = bi.fetch_add(1)
    if ob != 1000000000000 { print("ATOMIC FAIL i64 add ret") return }
    if bi.get() != 1000000000001 { print("ATOMIC FAIL i64 add val") return }

    // ---- f64 (RMW FAdd) ----
    Atomic(f64) fa = {}
    fa.set(1.5)
    f64 of = fa.fetch_add(2.25)
    if of != 1.5 { print("ATOMIC FAIL f64 add ret") return }
    if fa.get() != 3.75 { print("ATOMIC FAIL f64 add val") return }

    // ---- char (i8, byte-sized) ----
    Atomic(char) ch = {}
    ch.set('A')
    char oc = ch.swap('Z')
    if oc != 'A' { print("ATOMIC FAIL char swap") return }
    if ch.get() != 'Z' { print("ATOMIC FAIL char swap val") return }

    print("ATOMIC OK")
}
