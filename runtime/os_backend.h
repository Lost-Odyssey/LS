/* os_backend.h — Platform-neutral OS backend API for LS runtime.
 *
 * Implemented by os_win32.c (Windows) or os_posix.c (POSIX/Linux/macOS).
 * CMakeLists.txt routes the correct source file based on platform.
 *
 * Naming convention: ls_os_* (project-namespaced, not __ls_ which is for
 * LS-FFI-visible symbols).  These are internal C-to-C linkage symbols that
 * builtins.c calls; they are NOT directly visible to LS extern fn declarations.
 *
 * Phase 1 covers exec_full and env_all.
 * Phase 1 Step #2 will add: ls_os_popen / ls_os_pclose / ls_os_pid /
 *   ls_os_wait_exit_code (to eliminate #if in proc.ls).
 * Phase 1 Step #3 will add: ls_os_setenv / ls_os_unsetenv (env.ls).
 */
#ifndef LS_OS_BACKEND_H
#define LS_OS_BACKEND_H

/* --- Process execution (exec_full backend) ---
 *
 * ls_os_exec_run() launches cmd via the platform shell, captures stdout and
 * stderr separately, and stores the results in internal globals.  The
 * take_* functions transfer heap ownership of the captured buffers to the
 * caller (internal pointer is set to NULL after the transfer, preventing
 * double-free on the next run).
 */
void        ls_os_exec_run(const char *cmd);
void       *ls_os_exec_stdout_ptr(void);
void       *ls_os_exec_stderr_ptr(void);
void       *ls_os_exec_take_stdout(void);
long long   ls_os_exec_stdout_len(void);
void       *ls_os_exec_take_stderr(void);
long long   ls_os_exec_stderr_len(void);
int         ls_os_exec_get_code(void);
int         ls_os_exec_get_ok(void);

/* --- Process helpers (popen / pread / pclose / pid / exit-code decoding) ---
 *
 * ls_os_popen() launches cmd via the platform shell with stdout (and stderr
 *   if the caller appends "2>&1") captured to an OS pipe.  Returns an opaque
 *   handle on success, NULL on failure.
 * ls_os_pread() reads up to maxsz bytes from the handle into buf.  Returns
 *   the number of bytes actually read (0 at EOF).
 * ls_os_pclose() closes the pipe, waits for the child process, and returns
 *   the decoded exit code (NOT a raw wait status — callers use it directly).
 *
 * Windows: implemented with CreateProcess + Win32 ReadFile/CloseHandle so
 *   that the handle is CRT-independent (no FILE* involved).  This avoids the
 *   "static-CRT FILE* passed to DLL-CRT fread" crash that occurs when ls.exe
 *   (/MT) opens a pipe but JIT-resolved fread uses the DLL CRT file table.
 * POSIX: thin wrappers around popen/fread/pclose (all same libc instance).
 *
 * ls_os_pid() returns the current process ID.
 * ls_os_wait_exit_code() decodes a raw system() wait status to an exit code:
 *   Windows: identity (system returns exit code directly).
 *   POSIX:   WEXITSTATUS(status).
 *   Note: ls_os_pclose() already returns the decoded exit code; this function
 *   is only needed for the proc.run() path that calls system().
 */
void     *ls_os_popen(const char *cmd);
long long  ls_os_pread(void *fp, void *buf, long long maxsz);
int        ls_os_pclose(void *fp);
int        ls_os_pid(void);
int        ls_os_wait_exit_code(int raw);

/* --- Environment variable access and mutation ---
 *
 * ls_os_getenv() returns the value of name or NULL if not set.
 *   The returned pointer is valid until the next call to ls_os_getenv().
 *   (Same lifetime semantics as POSIX getenv.)
 * ls_os_setenv() sets name=value, overwriting any existing value.
 * ls_os_unsetenv() removes name from the environment.
 * Both set/unset return 0 on success, non-zero on failure.
 *
 * Windows: uses Win32 GetEnvironmentVariableA / SetEnvironmentVariableA so
 *   that reads and writes go through the same (OS-level) environment block,
 *   regardless of which CRT instance (static /MT vs DLL) the caller uses.
 * POSIX: wraps getenv / setenv / unsetenv from libc.
 */
const char *ls_os_getenv(const char *name);
int ls_os_setenv(const char *name, const char *value);
int ls_os_unsetenv(const char *name);

/* --- File positioning and deletion (io.ls backend) ---
 *
 * ls_os_fseek64() seeks within fp using a 64-bit offset.  origin follows the
 *   SEEK_SET/CUR/END convention (0/1/2).  Returns 0 on success.
 * ls_os_ftell64() returns the current file position as a 64-bit integer,
 *   or -1 on error.
 * ls_os_unlink() deletes the file at path.  Returns 0 on success.
 *
 * Windows: _fseeki64 / _ftelli64 / _unlink (avoids the 2GB limit of fseek).
 * POSIX:   fseeko / ftello / unlink.
 */
int       ls_os_fseek64(void *fp, long long off, int origin);
long long ls_os_ftell64(void *fp);
int       ls_os_unlink(const char *path);

/* --- Environment variable snapshot ---
 *
 * ls_os_env_prepare() takes an OS-level snapshot of the current environment
 * into internal storage (strdup'd entries).  Subsequent ls_os_env_count()
 * and ls_os_env_entry(i) queries read from that snapshot.  Each call to
 * ls_os_env_prepare() frees the previous snapshot first.
 */
void        ls_os_env_prepare(void);
int         ls_os_env_count(void);
const char *ls_os_env_entry(int i);

/* --- High-resolution monotonic clock (perf.now backend) ---
 *
 * ls_os_perf_now() returns a monotonic timestamp in nanoseconds.  The epoch
 * is unspecified (arbitrary fixed point); only differences are meaningful.
 *
 * Windows: QueryPerformanceCounter scaled by QueryPerformanceFrequency.
 *          The frequency is cached on first call; scaling uses double
 *          arithmetic to avoid 64-bit overflow for typical uptime values.
 * POSIX:   clock_gettime(CLOCK_MONOTONIC) → tv_sec * 1e9 + tv_nsec.
 *
 * Note: perf.rdtsc / perf.rdtscp are emitted as LLVM x86 intrinsics
 * directly in builtins_perf_cg.c; they require no OS backend symbol.
 * perf.elapsed_* are also fully inlined in IR (call now() + arithmetic).
 */
long long ls_os_perf_now(void);

/* ls_os_perf_rdtsc() returns the RDTSC cycle counter (non-serialising).
 * Windows: __rdtsc (intrin.h).  POSIX: __builtin_ia32_rdtsc().
 * Falls back to ls_os_perf_now() on non-x86 platforms. */
long long ls_os_perf_rdtsc(void);

/* ls_os_perf_rdtscp() returns the serialising RDTSCP timestamp.
 * Windows: __rdtscp (intrin.h).  POSIX: __builtin_ia32_rdtscp / inline asm.
 * Falls back to RDTSC on non-x86 platforms. */
long long ls_os_perf_rdtscp(void);

/* --- Directory listing ---
 *
 * ls_os_listdir_prepare() scans path and stores all entry names (excluding
 *   "." and "..") into internal storage (strdup'd).  Each call frees the
 *   previous snapshot first.  On error (path not found etc.) the count
 *   is set to 0 and no error is reported — callers get an empty result.
 * ls_os_listdir_count() returns the number of entries from the last prepare.
 * ls_os_listdir_entry(i) returns the i-th name; pointer owned by backend.
 *
 * Windows: FindFirstFileA / FindNextFileA / FindClose.
 * POSIX:   opendir / readdir / closedir.
 */
void        ls_os_listdir_prepare(const char *path);
int         ls_os_listdir_count(void);
const char *ls_os_listdir_entry(int i);

/* --- Filesystem / path operations (fs.ls backend) ---
 *
 * All functions return 0 on success, -1 on failure.
 * On failure, ls_os_last_error() returns a human-readable message.
 *
 * ls_os_path_exists()  — true if path exists (file or directory).
 * ls_os_path_is_dir()  — true if path exists and is a directory.
 * ls_os_path_is_file() — true if path exists and is a regular file.
 * ls_os_mkdir()        — create one directory level; fails if already exists.
 * ls_os_mkdir_all()    — create directory and all missing parents (like mkdir -p).
 * ls_os_rmdir()        — remove an empty directory.
 * ls_os_rename()       — rename / move a file or directory.
 * ls_os_getcwd()       — return current working directory (static buffer, never NULL).
 * ls_os_chdir()        — change current working directory.
 * ls_os_last_error()   — human-readable description of the last fs error.
 *
 * Windows: GetFileAttributesA / CreateDirectoryA / RemoveDirectoryA /
 *          MoveFileExA / GetCurrentDirectoryA / SetCurrentDirectoryA.
 * POSIX:   stat / mkdir / rmdir / rename / getcwd / chdir.
 */
int         ls_os_path_exists(const char *path);
int         ls_os_path_is_dir(const char *path);
int         ls_os_path_is_file(const char *path);
int         ls_os_mkdir(const char *path);
int         ls_os_mkdir_all(const char *path);
int         ls_os_rmdir(const char *path);
int         ls_os_rename_path(const char *from, const char *to);
const char *ls_os_getcwd(void);
int         ls_os_chdir(const char *path);
const char *ls_os_last_error(void);

/* --- Calendar / wall-clock time (std.time backend) ---
 *
 * ls_os_time_now_unix_ns() / ls_os_time_now_unix_ms():
 *   Return nanoseconds / milliseconds since the Unix epoch (1970-01-01 UTC).
 *   Uses FILETIME (Windows) or clock_gettime(CLOCK_REALTIME) (POSIX).
 *
 * ls_os_time_from_unix_local() / ls_os_time_from_unix_utc():
 *   Convert a Unix timestamp (seconds) to broken-down time, storing the
 *   result in internal state.  Fields are then readable via ls_os_time_get_*.
 *
 * ls_os_time_get_*:
 *   Query fields from the last from_unix_local/utc or time_parse call.
 *   weekday: 0=Mon..6=Sun (ISO 8601).
 *   utcoff:  seconds east of UTC (positive for UTC+n, negative for UTC-n).
 *
 * ls_os_time_to_unix():
 *   Convert broken-down time back to a Unix timestamp.
 *   is_utc=1 treats input as UTC, 0 as local time.  Returns -1 on failure.
 *
 * ls_os_time_format():
 *   Wrap strftime.  Returns pointer to a static 256-byte buffer.
 *   weekday parameter uses LS convention (0=Mon); converted internally.
 *
 * ls_os_time_parse():
 *   Parse text with the given strftime-style format.  Returns 1 on success,
 *   0 on failure.  Fills the same internal state as from_unix_local/utc.
 *   Windows: only %Y %m %d %H %M %S are supported (sscanf-based).
 *   POSIX:   any strptime format is accepted.
 *
 * ls_os_sleep_ms() / ls_os_sleep_us():
 *   Block the calling thread for at least the given duration.
 *   Windows: Sleep (ms resolution) / NtDelayExecution shim.
 *   POSIX:   nanosleep.
 */
long long   ls_os_time_now_unix_ns(void);
long long   ls_os_time_now_unix_ms(void);
void        ls_os_time_from_unix_local(long long unix_s);
void        ls_os_time_from_unix_utc(long long unix_s);
int         ls_os_time_get_year(void);
int         ls_os_time_get_month(void);
int         ls_os_time_get_day(void);
int         ls_os_time_get_hour(void);
int         ls_os_time_get_minute(void);
int         ls_os_time_get_second(void);
int         ls_os_time_get_weekday(void);   /* 0=Mon..6=Sun */
int         ls_os_time_get_yday(void);
int         ls_os_time_get_utcoff(void);    /* seconds east of UTC */
long long   ls_os_time_to_unix(int year, int month, int day,
                                int hour,  int minute, int second,
                                int is_utc);
const char *ls_os_time_format(int year, int month, int day,
                               int hour,  int minute, int second,
                               int weekday, int yday,
                               const char *fmt);
int         ls_os_time_parse(const char *text, const char *fmt);
void        ls_os_sleep_ms(long long ms);
void        ls_os_sleep_us(long long us);

#endif /* LS_OS_BACKEND_H */
