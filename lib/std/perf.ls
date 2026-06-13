// std/perf.ls — High-resolution timing primitives.
// Pure LS wrapper over C backend functions in runtime/os_win32.c / os_posix.c.

extern fn ls_os_perf_now() -> i64
extern fn ls_os_perf_rdtsc() -> i64
extern fn ls_os_perf_rdtscp() -> i64

// Monotonic wall-clock timestamp in nanoseconds.
// Epoch is unspecified; only differences are meaningful.
fn now() -> i64 {
    return ls_os_perf_now()
}

// CPU cycle counter via RDTSC (non-serialising).
// Use rdtscp() for micro-benchmarks that require ordering guarantees.
fn rdtsc() -> i64 {
    return ls_os_perf_rdtsc()
}

// Serialising RDTSCP cycle counter.
// Falls back to now() on non-x86 platforms.
fn rdtscp() -> i64 {
    return ls_os_perf_rdtscp()
}

// Nanoseconds elapsed since t0.
fn elapsed_ns(i64 t0) -> i64 {
    return ls_os_perf_now() - t0
}

// Milliseconds elapsed since t0 (f64).
fn elapsed_ms(i64 t0) -> f64 {
    i64 ns = ls_os_perf_now() - t0
    return ns as f64 / 1000000.0
}

// Seconds elapsed since t0 (f64).
fn elapsed_s(i64 t0) -> f64 {
    i64 ns = ls_os_perf_now() - t0
    return ns as f64 / 1000000000.0
}
