// std.sync.chan for-in — `for x in ch` drains the channel (blocking on empty) until
// it is closed AND empty. A producer thread sends N items then closes; the main
// thread consumes the whole stream with for-in. Exercises the Iterator(T)
// protocol over a channel (ChanIter holds a raw *Chan; next() = blocking recv).
//
// NO --memcheck (threaded). Soundness via JIT + repeated AOT (a broken iterator
// would skew the count or hang).

import std.sync.chan
import std.sync.task

Chan(int) g_ch = {}

def main() {
    g_ch = channel(int)(32)
    int N = 50000

    Task(int) prod = {}
    prod.run(|| {
        for i in 0..50000 {
            bool ok = g_ch.send(i)
        }
        g_ch.close()              // for-in ends once drained after this
        return 0
    })

    i64 sum = 0
    int got = 0
    for x in g_ch {               // blocks on empty, ends when closed & drained
        sum = sum + (x as i64)
        got = got + 1
    }
    prod.join()

    i64 expect = (N as i64) * ((N as i64) - 1) / 2
    if got != N {
        @print("FORIN FAIL count")
        @print(got)
        return
    }
    if sum != expect {
        @print("FORIN FAIL sum")
        @print(sum)
        return
    }
    @print("FORIN OK")
}
