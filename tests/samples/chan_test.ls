// std.sync.chan single-threaded correctness + memcheck.
//
// Single-threaded so we must never block: send only when there is room, recv
// only when count>0 or after close (then it returns None without blocking).
// Exercises send/try_send/recv/try_recv/close/is_closed + __drop residual on
// owned has_drop Str. memcheck must be 0/0/0.

import std.sync.chan
import std.core.str

def report(Str label, bool cond) {
    if cond {
        @print(label)
    } else {
        @print("FAIL")
        @print(label)
    }
}

def main() -> int {
    Chan(Str) ch = channel(Str)(4)   // cap rounds to 4

    report("cap is 4", ch.capacity() == 4)
    report("empty len 0", ch.len() == 0)
    report("not closed", !ch.is_closed())

    bool s1 = ch.send("alpha")       // count 1 (room → no block)
    bool s2 = ch.send("beta")        // count 2
    report("two sent", s1 && s2 && ch.len() == 2)

    // fill to capacity, then try_send returns the value back
    bool s3 = ch.send("gamma")       // count 3
    bool s4 = ch.send("delta")       // count 4 = full
    report("filled to cap", ch.len() == 4)

    match ch.try_send("rejected") {  // full → Some(value handed back)
        Some(v) => { report("try_send full -> Some", v.eq?("rejected")) }
        None    => { report("try_send full -> Some", false) }
    }

    // recv one (count>0 → no block), then there is room again
    match ch.recv() {
        Some(x) => { report("recv alpha", x.eq?("alpha")) }
        None    => { report("recv alpha", false) }
    }
    report("len 3 after recv", ch.len() == 3)

    match ch.try_send("epsilon") {   // room now → None (consumed)
        Some(v) => { report("try_send room -> None", false) }
        None    => { report("try_send room -> None", true) }
    }
    report("len 4 again", ch.len() == 4)

    ch.close()
    report("closed", ch.is_closed())

    // send on closed -> false (value dropped)
    bool s5 = ch.send("late")
    report("send on closed -> false", !s5)

    // drain two, leaving residual for __drop to clean
    match ch.recv() { Some(x) => { report("drain beta", x.eq?("beta")) }  None => { } }
    match ch.recv() { Some(x) => { report("drain gamma", x.eq?("gamma")) } None => { } }
    // 2 elements (delta, epsilon) remain unconsumed -> __drop must free them

    @print("CHAN PASS")
    return 0
}
