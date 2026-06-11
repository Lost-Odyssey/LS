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
// WARNING: raw_* functions are implementation details of the stdlib.
// User code should NOT import std.os or call raw_* directly.
// Use the high-level stdlib modules (std.proc, std.env, std.io) instead.


// ---- Process execution backend (exec_full) ----

extern fn ls_os_exec_run(string cmd)
extern fn ls_os_exec_take_stdout() -> object
extern fn ls_os_exec_take_stderr() -> object
extern fn ls_os_exec_stdout_ptr() -> object
extern fn ls_os_exec_stderr_ptr() -> object
extern fn ls_os_exec_stdout_len() -> i64
extern fn ls_os_exec_stderr_len() -> i64
extern fn ls_os_exec_get_code() -> int
extern fn ls_os_exec_get_ok() -> int

fn raw_exec_run(string cmd) { ls_os_exec_run(cmd) }
fn raw_exec_take_stdout() -> object { return ls_os_exec_take_stdout() }
fn raw_exec_stdout_ptr() -> object { return ls_os_exec_stdout_ptr() }
fn raw_exec_stderr_ptr() -> object { return ls_os_exec_stderr_ptr() }
fn raw_exec_take_stderr() -> object { return ls_os_exec_take_stderr() }
fn raw_exec_stdout_len() -> i64 { return ls_os_exec_stdout_len() }
fn raw_exec_stderr_len() -> i64 { return ls_os_exec_stderr_len() }
fn raw_exec_get_code() -> int { return ls_os_exec_get_code() }
fn raw_exec_get_ok() -> int { return ls_os_exec_get_ok() }

// ---- Process helpers (popen / pread / pclose / pid / exit-code decoding) ----

extern fn ls_os_popen(string cmd) -> object
extern fn ls_os_pread(object fp, object buf, i64 maxsz) -> i64
extern fn ls_os_pclose(object fp) -> int
extern fn ls_os_pid() -> int
extern fn ls_os_wait_exit_code(int raw) -> int

fn raw_popen(string cmd) -> object { return ls_os_popen(cmd) }
fn raw_pread(object fp, object buf, i64 maxsz) -> i64 { return ls_os_pread(fp, buf, maxsz) }
fn raw_pclose(object fp) -> int { return ls_os_pclose(fp) }
fn raw_pid() -> int { return ls_os_pid() }
fn raw_wait_exit_code(int r) -> int { return ls_os_wait_exit_code(r) }

// ---- Environment variable access and mutation ----

extern fn ls_os_getenv(string name) -> object
extern fn ls_os_setenv(string name, string value) -> int
extern fn ls_os_unsetenv(string name) -> int

fn raw_getenv(string name) -> object { return ls_os_getenv(name) }
fn raw_setenv(string name, string value) -> int { return ls_os_setenv(name, value) }
fn raw_unsetenv(string name) -> int { return ls_os_unsetenv(name) }

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
extern fn ls_os_unlink(string path) -> int

fn raw_fseek64(object fp, i64 off, int origin) -> int { return ls_os_fseek64(fp, off, origin) }
fn raw_ftell64(object fp) -> i64 { return ls_os_ftell64(fp) }
fn raw_unlink(string path) -> int { return ls_os_unlink(path) }

// ---- Directory listing (os_win32.c / os_posix.c) ----

extern fn ls_os_listdir_prepare(string path)
extern fn ls_os_listdir_count() -> int
extern fn ls_os_listdir_entry(int i) -> object

fn raw_listdir_prepare(string path) { ls_os_listdir_prepare(path) }
fn raw_listdir_count() -> int { return ls_os_listdir_count() }
fn raw_listdir_entry(int i) -> object { return ls_os_listdir_entry(i) }

// ---- Filesystem / path operations (os_win32.c / os_posix.c) ----

extern fn ls_os_path_exists(string path) -> int
extern fn ls_os_path_is_dir(string path) -> int
extern fn ls_os_path_is_file(string path) -> int
extern fn ls_os_mkdir(string path) -> int
extern fn ls_os_mkdir_all(string path) -> int
extern fn ls_os_rmdir(string path) -> int
extern fn ls_os_rename_path(string from_path, string to_path) -> int
extern fn ls_os_getcwd() -> object
extern fn ls_os_chdir(string path) -> int
extern fn ls_os_last_error() -> object

fn raw_path_exists(string path) -> int  { return ls_os_path_exists(path) }
fn raw_path_is_dir(string path) -> int  { return ls_os_path_is_dir(path) }
fn raw_path_is_file(string path) -> int { return ls_os_path_is_file(path) }
fn raw_mkdir(string path) -> int        { return ls_os_mkdir(path) }
fn raw_mkdir_all(string path) -> int    { return ls_os_mkdir_all(path) }
fn raw_rmdir(string path) -> int        { return ls_os_rmdir(path) }
fn raw_rename_path(string f, string t) -> int { return ls_os_rename_path(f, t) }
fn raw_getcwd() -> object               { return ls_os_getcwd() }
fn raw_chdir(string path) -> int        { return ls_os_chdir(path) }
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
extern fn ls_os_time_format(int year, int month, int day, int hour, int minute, int second, int weekday, int yday, string fmt) -> object
extern fn ls_os_time_parse(string text, string fmt) -> int
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
fn raw_time_format(int year, int month, int day, int hour, int minute, int second, int weekday, int yday, string fmt) -> object {
    return ls_os_time_format(year, month, day, hour, minute, second, weekday, yday, fmt)
}
fn raw_time_parse(string text, string fmt) -> int { return ls_os_time_parse(text, fmt) }
fn raw_sleep_ms(i64 ms) { ls_os_sleep_ms(ms) }
fn raw_sleep_us(i64 us) { ls_os_sleep_us(us) }
