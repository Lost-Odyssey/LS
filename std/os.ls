// std/os.ls — OS backend module for LS stdlib.
//
// This is the ONLY file in the LS stdlib that declares `extern fn ls_os_*`
// symbols.  All other stdlib modules (std.proc, std.env, std.io) import this
// module and call the `raw_*` wrappers defined here instead of calling
// ls_os_* directly.
//
// Naming convention:
//   ls_os_*  — internal C symbols (runtime/os_win32.c or os_posix.c)
//   raw_*    — public LS wrappers exported by this module
//
// FFI boundary (P5-3): extern fn take raw `*u8` (NUL-terminated char*); the
// raw_* wrappers take `Str` and pass `str.c_str()` (Str.c_str guarantees a
// NUL terminator). Callers already hold Str, so they pass it through unchanged.
//
// WARNING: raw_* functions are implementation details of the stdlib.
// User code should NOT import std.os or call raw_* directly.
// Use the high-level stdlib modules (std.proc, std.env, std.io) instead.

import std.str


// ---- Process execution backend (exec_full) ----

extern fn ls_os_exec_run(*u8 cmd)
extern fn ls_os_exec_take_stdout() -> object
extern fn ls_os_exec_take_stderr() -> object
extern fn ls_os_exec_stdout_ptr() -> object
extern fn ls_os_exec_stderr_ptr() -> object
extern fn ls_os_exec_stdout_len() -> i64
extern fn ls_os_exec_stderr_len() -> i64
extern fn ls_os_exec_get_code() -> int
extern fn ls_os_exec_get_ok() -> int

fn raw_exec_run(Str cmd) { ls_os_exec_run(cmd.c_str()) }
fn raw_exec_take_stdout() -> object { return ls_os_exec_take_stdout() }
fn raw_exec_stdout_ptr() -> object { return ls_os_exec_stdout_ptr() }
fn raw_exec_stderr_ptr() -> object { return ls_os_exec_stderr_ptr() }
fn raw_exec_take_stderr() -> object { return ls_os_exec_take_stderr() }
fn raw_exec_stdout_len() -> i64 { return ls_os_exec_stdout_len() }
fn raw_exec_stderr_len() -> i64 { return ls_os_exec_stderr_len() }
fn raw_exec_get_code() -> int { return ls_os_exec_get_code() }
fn raw_exec_get_ok() -> int { return ls_os_exec_get_ok() }

// ---- Process helpers (popen / pread / pclose / pid / exit-code decoding) ----

extern fn ls_os_popen(*u8 cmd) -> object
extern fn ls_os_pread(object fp, object buf, i64 maxsz) -> i64
extern fn ls_os_pclose(object fp) -> int
extern fn ls_os_pid() -> int
extern fn ls_os_wait_exit_code(int raw) -> int

fn raw_popen(Str cmd) -> object { return ls_os_popen(cmd.c_str()) }
fn raw_pread(object fp, object buf, i64 maxsz) -> i64 { return ls_os_pread(fp, buf, maxsz) }
fn raw_pclose(object fp) -> int { return ls_os_pclose(fp) }
fn raw_pid() -> int { return ls_os_pid() }
fn raw_wait_exit_code(int r) -> int { return ls_os_wait_exit_code(r) }

// ---- Environment variable access and mutation ----

extern fn ls_os_getenv(*u8 name) -> object
extern fn ls_os_setenv(*u8 name, *u8 value) -> int
extern fn ls_os_unsetenv(*u8 name) -> int

fn raw_getenv(Str name) -> object { return ls_os_getenv(name.c_str()) }
fn raw_setenv(Str name, Str value) -> int { return ls_os_setenv(name.c_str(), value.c_str()) }
fn raw_unsetenv(Str name) -> int { return ls_os_unsetenv(name.c_str()) }

// ---- Environment variable snapshot ----

extern fn ls_os_env_prepare()
extern fn ls_os_env_count() -> int
extern fn ls_os_env_entry(int i) -> object

fn raw_env_prepare() { ls_os_env_prepare() }
fn raw_env_count() -> int { return ls_os_env_count() }
fn raw_env_entry(int i) -> object { return ls_os_env_entry(i) }

// ---- File positioning and deletion (std.io backend) ----

extern fn ls_os_fseek64(object fp, i64 off, int origin) -> int
extern fn ls_os_ftell64(object fp) -> i64
extern fn ls_os_unlink(*u8 path) -> int

fn raw_fseek64(object fp, i64 off, int origin) -> int { return ls_os_fseek64(fp, off, origin) }
fn raw_ftell64(object fp) -> i64 { return ls_os_ftell64(fp) }
fn raw_unlink(Str path) -> int { return ls_os_unlink(path.c_str()) }

// ---- Directory listing (os_win32.c / os_posix.c) ----

extern fn ls_os_listdir_prepare(*u8 path)
extern fn ls_os_listdir_count() -> int
extern fn ls_os_listdir_entry(int i) -> object

fn raw_listdir_prepare(Str path) { ls_os_listdir_prepare(path.c_str()) }
fn raw_listdir_count() -> int { return ls_os_listdir_count() }
fn raw_listdir_entry(int i) -> object { return ls_os_listdir_entry(i) }

// ---- Filesystem / path operations (os_win32.c / os_posix.c) ----

extern fn ls_os_path_exists(*u8 path) -> int
extern fn ls_os_path_is_dir(*u8 path) -> int
extern fn ls_os_path_is_file(*u8 path) -> int
extern fn ls_os_mkdir(*u8 path) -> int
extern fn ls_os_mkdir_all(*u8 path) -> int
extern fn ls_os_rmdir(*u8 path) -> int
extern fn ls_os_rename_path(*u8 from_path, *u8 to_path) -> int
extern fn ls_os_getcwd() -> object
extern fn ls_os_chdir(*u8 path) -> int
extern fn ls_os_last_error() -> object

fn raw_path_exists(Str path) -> int  { return ls_os_path_exists(path.c_str()) }
fn raw_path_is_dir(Str path) -> int  { return ls_os_path_is_dir(path.c_str()) }
fn raw_path_is_file(Str path) -> int { return ls_os_path_is_file(path.c_str()) }
fn raw_mkdir(Str path) -> int        { return ls_os_mkdir(path.c_str()) }
fn raw_mkdir_all(Str path) -> int    { return ls_os_mkdir_all(path.c_str()) }
fn raw_rmdir(Str path) -> int        { return ls_os_rmdir(path.c_str()) }
fn raw_rename_path(Str f, Str t) -> int { return ls_os_rename_path(f.c_str(), t.c_str()) }
fn raw_getcwd() -> object               { return ls_os_getcwd() }
fn raw_chdir(Str path) -> int        { return ls_os_chdir(path.c_str()) }
fn raw_last_error() -> object           { return ls_os_last_error() }

// ---- Calendar / wall-clock time (time.ls backend) ----

extern fn ls_os_time_now_unix_ns() -> i64
extern fn ls_os_time_now_unix_ms() -> i64
extern fn ls_os_time_from_unix_local(i64 unix_s)
extern fn ls_os_time_from_unix_utc(i64 unix_s)
extern fn ls_os_time_get_year() -> int
extern fn ls_os_time_get_month() -> int
extern fn ls_os_time_get_day() -> int
extern fn ls_os_time_get_hour() -> int
extern fn ls_os_time_get_minute() -> int
extern fn ls_os_time_get_second() -> int
extern fn ls_os_time_get_weekday() -> int
extern fn ls_os_time_get_yday() -> int
extern fn ls_os_time_get_utcoff() -> int
extern fn ls_os_time_to_unix(int year, int month, int day, int hour, int minute, int second, int is_utc) -> i64
extern fn ls_os_time_format(int year, int month, int day, int hour, int minute, int second, int weekday, int yday, *u8 fmt) -> object
extern fn ls_os_time_parse(*u8 text, *u8 fmt) -> int
extern fn ls_os_sleep_ms(i64 ms)
extern fn ls_os_sleep_us(i64 us)

fn raw_time_now_unix_ns() -> i64 { return ls_os_time_now_unix_ns() }
fn raw_time_now_unix_ms() -> i64 { return ls_os_time_now_unix_ms() }
fn raw_time_from_unix_local(i64 unix_s) { ls_os_time_from_unix_local(unix_s) }
fn raw_time_from_unix_utc(i64 unix_s)   { ls_os_time_from_unix_utc(unix_s) }
fn raw_time_get_year() -> int    { return ls_os_time_get_year() }
fn raw_time_get_month() -> int   { return ls_os_time_get_month() }
fn raw_time_get_day() -> int     { return ls_os_time_get_day() }
fn raw_time_get_hour() -> int    { return ls_os_time_get_hour() }
fn raw_time_get_minute() -> int  { return ls_os_time_get_minute() }
fn raw_time_get_second() -> int  { return ls_os_time_get_second() }
fn raw_time_get_weekday() -> int { return ls_os_time_get_weekday() }
fn raw_time_get_yday() -> int    { return ls_os_time_get_yday() }
fn raw_time_get_utcoff() -> int  { return ls_os_time_get_utcoff() }
fn raw_time_to_unix(int year, int month, int day, int hour, int minute, int second, int is_utc) -> i64 {
    return ls_os_time_to_unix(year, month, day, hour, minute, second, is_utc)
}
fn raw_time_format(int year, int month, int day, int hour, int minute, int second, int weekday, int yday, Str fmt) -> object {
    return ls_os_time_format(year, month, day, hour, minute, second, weekday, yday, fmt.c_str())
}
fn raw_time_parse(Str text, Str fmt) -> int { return ls_os_time_parse(text.c_str(), fmt.c_str()) }
fn raw_sleep_ms(i64 ms) { ls_os_sleep_ms(ms) }
fn raw_sleep_us(i64 us) { ls_os_sleep_us(us) }
