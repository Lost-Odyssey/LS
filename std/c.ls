// std/c.ls — Centralised C / CRT bindings for the LS stdlib.
//
// This is the ONLY file that declares extern fn for standard C library
// functions and LS runtime helpers.  All other stdlib modules (std.io,
// std.proc, std.env) import this module and call c.xxx() instead of
// declaring their own extern fn.
//
// Naming: functions keep their original C names so call sites are
// immediately recognisable to anyone who knows C.


// ---- C standard library (CRT) ----

extern {
    // P5-3: char* params take raw `*u8` (NUL-terminated). Callers pass
    // `str.c_str()` (Str.c_str guarantees a NUL terminator).
    fn fopen(*u8 path, *u8 mode) -> object
    fn fclose(object fp) -> int
    fn fread(*u8 buf, i64 sz, i64 n, object fp) -> i64
    fn fwrite(*u8 buf, i64 sz, i64 n, object fp) -> i64
    fn strlen(*u8 s) -> i64
    fn system(*u8 cmd) -> int
    fn strerror(int e) -> object
    // Raw heap management (CRT). size_t is 64-bit on x64 → i64. These bind to the
    // real CRT symbols (no __ls_ prefix — that's reserved for runtime-owned
    // symbols). Reached as std.c.malloc / std.c.realloc / std.c.free, or via an
    // import alias (c.malloc). See docs/plan_runtime_primitives.md.
    fn malloc(i64 sz) -> *u8
    fn realloc(*u8 p, i64 sz) -> *u8
    fn free(*u8 p)
}

// Abort the process with exit code 1 — the controlled-exit semantics the stdlib
// bounds checks rely on (NOT the CRT abort()'s SIGABRT). A real LS fn (symbol
// std_c__abort), reached as std.c.abort().
fn abort() { __ls_proc_exit(1) }

// ---- LS runtime helpers (runtime/builtins.c) ----
// These are cross-platform, no #ifdef needed — builtins.c is the sole owner.

extern fn __ls_get_argc() -> int
extern fn __ls_get_argv(int i) -> object
extern fn __ls_proc_exit(int code)

// ---- stdin readline (runtime/builtins.c) ----

extern fn __ls_readline_exec()
extern fn __ls_readline_ok() -> int
extern fn __ls_readline_len() -> i64
extern fn __ls_readline_take() -> object
extern fn __ls_readline_ptr() -> object

// ---- regex engine (runtime/ls_regex.c) ----

extern fn __ls_regex_compile(*u8 pattern, int flags) -> int
extern fn __ls_regex_free(int handle)
extern fn __ls_regex_last_error() -> object
extern fn __ls_regex_exec(int handle, *u8 text, int text_len, int start) -> int
extern fn __ls_regex_cap_start(int group) -> int
extern fn __ls_regex_cap_len(int group) -> int
extern fn __ls_regex_group_count(int handle) -> int
extern fn __ls_regex_named_count(int handle) -> int
extern fn __ls_regex_named_name(int handle, int i) -> object
extern fn __ls_regex_named_index(int handle, int i) -> int
