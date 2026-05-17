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
    fn fopen(string path, string mode) -> object
    fn fclose(object fp) -> int
    fn fread(*u8 buf, i64 sz, i64 n, object fp) -> i64
    fn fwrite(*u8 buf, i64 sz, i64 n, object fp) -> i64
    fn strlen(string s) -> i64
    fn system(string cmd) -> int
    fn strerror(int e) -> object
}

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

// ---- regex engine (runtime/ls_regex.c) ----

extern fn __ls_regex_compile(string pattern, int flags) -> int
extern fn __ls_regex_free(int handle)
extern fn __ls_regex_last_error() -> object
extern fn __ls_regex_exec(int handle, string text, int text_len, int start) -> int
extern fn __ls_regex_cap_start(int group) -> int
extern fn __ls_regex_cap_len(int group) -> int
extern fn __ls_regex_group_count(int handle) -> int
extern fn __ls_regex_named_count(int handle) -> int
extern fn __ls_regex_named_name(int handle, int i) -> object
extern fn __ls_regex_named_index(int handle, int i) -> int
