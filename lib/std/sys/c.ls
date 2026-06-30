// std/c.ls — Centralised C / CRT bindings for the LS stdlib.
//
// This is the ONLY file that declares extern def for standard C library
// functions and LS runtime helpers.  All other stdlib modules (std.sys.io,
// std.sys.proc, std.sys.env) import this module and call c.xxx() instead of
// declaring their own extern def.
//
// Naming: functions keep their original C names so call sites are
// immediately recognisable to anyone who knows C.


// ---- C standard library (CRT) ----

extern {
    // P5-3: char* params take raw `*u8` (NUL-terminated). Callers pass
    // `str.c_str()` (Str.c_str guarantees a NUL terminator).
    def fopen(*u8 path, *u8 mode) -> object
    def fclose(object fp) -> int
    def fflush(object fp) -> int
    def fread(*u8 buf, i64 sz, i64 n, object fp) -> i64
    def fwrite(*u8 buf, i64 sz, i64 n, object fp) -> i64
    def strlen(*u8 s) -> i64
    def system(*u8 cmd) -> int
    def strerror(int e) -> object
    // Raw heap management (CRT). size_t is 64-bit on x64 → i64. These bind to the
    // real CRT symbols (no __ls_ prefix — that's reserved for runtime-owned
    // symbols). Reached as std.sys.c.malloc / std.sys.c.realloc / std.sys.c.free, or via an
    // import alias (c.malloc). See docs/plan_runtime_primitives.md.
    def malloc(i64 sz) -> *u8
    def realloc(*u8 p, i64 sz) -> *u8
    def free(*u8 p)
}

// Abort the process with exit code 1 — the controlled-exit semantics the stdlib
// bounds checks rely on (NOT the CRT abort()'s SIGABRT). A real LS def (symbol
// std_c__abort), reached as std.sys.c.abort().
def abort() { __ls_proc_exit(1) }

// ---- LS runtime helpers (runtime/builtins.c) ----
// These are cross-platform, no #ifdef needed — builtins.c is the sole owner.

extern def __ls_get_argc() -> int
extern def __ls_get_argv(int i) -> object
extern def __ls_proc_exit(int code)

// Substring search over raw ptr+len buffers (memchr+memcmp, SIMD-accelerated in
// the CRT). No NUL termination assumed — safe to call on a read-only &Str's data.
// Returns the byte offset of `needle` in `hay` at/after `start`, or -1.
extern def __ls_str_find(*u8 hay, int hlen, *u8 needle, int nlen, int start) -> int

// Copy `n` bytes from src+soff to dst+doff (one memcpy, SIMD in the CRT). Byte
// offsets because LS has no pointer arithmetic. n <= 0 is a no-op.
extern def __ls_bytecopy(*u8 dst, int doff, *u8 src, int soff, int n)

// Pointer arithmetic primitive: returns base + off (byte offset). Lets a
// region/arena carve a typed `*T` slice from one `*u8` block. No bounds check.
extern def __ls_ptr_at(*u8 base, i64 off) -> *u8

// The CRT's stdout/stderr FILE* (as `object`) for std.core.sink redirect — handed
// straight to c.fwrite. Same-TU CRT as the runtime's print path.
extern def __ls_stdout() -> object
extern def __ls_stderr() -> object

// @print() redirect (std.core.sink): __ls_sink_set points the print stream at fp
// (owned=1 => fclose on next switch); __ls_sink_stream returns the current FILE*.
// @print()'s codegen writes to the same stream via __ls_printf.
extern def __ls_sink_set(object fp, int owned)
extern def __ls_sink_stream() -> object

// Float -> text via a runtime static buffer (snprintf "%.*f"): exec formats,
// ptr returns the NUL-terminated buffer (copy before the next exec). Used by
// std.text.strconv and std.core.sink (Sink.write_f64). digits=6 == default "%f".
extern def __ls_float_fixed_exec(f64 val, int digits)
extern def __ls_float_fixed_ptr() -> object

// Byte-wise FxHash over a raw ptr+len buffer (one C loop, no NUL assumed).
// Bit-identical to the old per-byte LS hash loop; replaces 2.5M+ per-byte
// fx_mix calls in the Map(Str,_) hot path. len <= 0 returns 0.
extern def __ls_fxhash_bytes(*u8 data, int len) -> u64

// Number of logical processors (par_for default worker fan-out). >= 1.
extern def __ls_cpu_count() -> int

// Cache size in KB for `level`: 1=L1d (per core), 2=L2 (per core), 3=L3 (shared).
// 0 if unknown. Drives std.sci.nn.sgemm_packed's analytical cache blocking.
extern def __ls_cache_kb(int level) -> int

// 1 if the host supports AVX-512 Foundation, else 0. std.sci.nn.sgemm uses it to
// pick the 12x32 (AVX-512) vs 6x16 (AVX2) micro-kernel.
extern def __ls_cpu_has_avx512() -> int

// Byte-buffer integer loads (std.text.bytes — V2 bit-pattern parsing). Assemble an
// N-byte big/little-endian integer from p+off by byte shifts (host-endian
// independent). All return u64 (value zero-extended); the std.text.bytes wrapper casts
// down. NO bounds check here — std.text.bytes.Reader validates before calling.
extern def __ls_load_u8(*u8 p, i64 off) -> u64
extern def __ls_load_be_u16(*u8 p, i64 off) -> u64
extern def __ls_load_be_u32(*u8 p, i64 off) -> u64
extern def __ls_load_be_u64(*u8 p, i64 off) -> u64
extern def __ls_load_le_u16(*u8 p, i64 off) -> u64
extern def __ls_load_le_u32(*u8 p, i64 off) -> u64
extern def __ls_load_le_u64(*u8 p, i64 off) -> u64

// ---- stdin readline (runtime/builtins.c) ----

extern def __ls_readline_exec()
extern def __ls_readline_ok() -> int
extern def __ls_readline_len() -> i64
extern def __ls_readline_take() -> object
extern def __ls_readline_ptr() -> object

// Note: std.sync.lock's mutex/spin primitives (ls_mutex_*, ls_cpu_relax) are NOT
// declared here — they are reached as compiler intrinsics (__mutex_*/__cpu_relax)
// so they survive generic-method instantiation without an import alias, the same
// way std.sync.task uses __task_*. See src/codegen.c and docs/plan_atomic_mutex.md.

// ---- regex engine (runtime/ls_regex.c) ----

extern def __ls_regex_compile(*u8 pattern, int flags) -> int
extern def __ls_regex_free(int handle)
extern def __ls_regex_last_error() -> object
extern def __ls_regex_exec(int handle, *u8 text, int text_len, int start) -> int
extern def __ls_regex_cap_start(int group) -> int
extern def __ls_regex_cap_len(int group) -> int
extern def __ls_regex_group_count(int handle) -> int
extern def __ls_regex_named_count(int handle) -> int
extern def __ls_regex_named_name(int handle, int i) -> object
extern def __ls_regex_named_index(int handle, int i) -> int
