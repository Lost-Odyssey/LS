import std.sys.perf as perf

def main() {
    // --- Test 1: perf.now() returns a positive i64 ---
    i64 t0 = perf.now()
    if t0 > 0 {
        @print("T1: now positive")
    }

    // --- Test 2: elapsed_ns returns a non-negative i64 ---
    i64 t1 = perf.now()
    i64 dt_ns = perf.elapsed_ns(t1)
    if dt_ns >= 0 {
        @print("T2: elapsed_ns ok")
    }

    // --- Test 3: elapsed_ms returns a non-negative f64 ---
    i64 t2 = perf.now()
    f64 dt_ms = perf.elapsed_ms(t2)
    if dt_ms >= 0.0 {
        @print("T3: elapsed_ms ok")
    }

    // --- Test 4: elapsed_s returns a non-negative f64 ---
    i64 t3 = perf.now()
    f64 dt_s = perf.elapsed_s(t3)
    if dt_s >= 0.0 {
        @print("T4: elapsed_s ok")
    }

    // --- Test 5: two consecutive now() calls are monotonic ---
    i64 ta = perf.now()
    i64 tb = perf.now()
    if tb >= ta {
        @print("T5: monotonic ok")
    }

    // --- Test 6: rdtsc returns a positive i64 ---
    i64 c0 = perf.rdtsc()
    if c0 > 0 {
        @print("T6: rdtsc positive")
    }

    // --- Test 7: rdtscp returns a positive i64 ---
    i64 c1 = perf.rdtscp()
    if c1 > 0 {
        @print("T7: rdtscp positive")
    }

    // --- Test 8: rdtsc is monotonic (two consecutive calls) ---
    i64 r0 = perf.rdtsc()
    i64 r1 = perf.rdtsc()
    if r1 >= r0 {
        @print("T8: rdtsc monotonic ok")
    }

    @print("ALL PASS")
}
