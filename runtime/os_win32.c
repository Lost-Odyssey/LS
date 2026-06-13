/* os_win32.c — Windows OS backend for LS runtime.
 *
 * This is the ONLY file in the project that uses Win32 API for process
 * execution and environment access.  All other LS runtime/stdlib files
 * call the ls_os_* symbols declared in os_backend.h.
 *
 * Compiled exclusively on Windows (CMakeLists routes based on platform).
 */

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <process.h>   /* _beginthreadex (CRT-aware thread start) */
#include "os_backend.h"

/* Forward-declare env-string helpers to avoid WIN32_LEAN_AND_MEAN issues */
__declspec(dllimport) char * __stdcall GetEnvironmentStringsA(void);
__declspec(dllimport) int   __stdcall FreeEnvironmentStringsA(char *);

/* =========================================================================
 * Process execution — exec_full backend
 * ========================================================================= */

static char  *g_exec_stdout     = NULL;
static size_t g_exec_stdout_len = 0;
static char  *g_exec_stderr     = NULL;
static size_t g_exec_stderr_len = 0;
static int    g_exec_code       = 0;
static int    g_exec_ok         = 0;

static char *read_handle_all(HANDLE h, size_t *out_len) {
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { *out_len = 0; return NULL; }
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(h, buf + len, (DWORD)(cap - len - 1), &n, NULL) || n == 0)
            break;
        len += (size_t)n;
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) break;
            buf = nb;
        }
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

void ls_os_exec_run(const char *cmd) {
    free(g_exec_stdout); g_exec_stdout = NULL; g_exec_stdout_len = 0;
    free(g_exec_stderr); g_exec_stderr = NULL; g_exec_stderr_len = 0;
    g_exec_code = 0;
    g_exec_ok   = 0;

    SECURITY_ATTRIBUTES sa;
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE;

    HANDLE out_r, out_w, err_r, err_w;
    if (!CreatePipe(&out_r, &out_w, &sa, 0)) return;
    if (!CreatePipe(&err_r, &err_w, &sa, 0)) {
        CloseHandle(out_r); CloseHandle(out_w); return;
    }
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = out_w;
    si.hStdError  = err_w;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    char cmd_buf[32768];
    snprintf(cmd_buf, sizeof(cmd_buf), "cmd.exe /c %s", cmd);

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    BOOL ok = CreateProcessA(NULL, cmd_buf, NULL, NULL,
                             TRUE, 0, NULL, NULL, &si, &pi);
    CloseHandle(out_w);
    CloseHandle(err_w);
    if (!ok) { CloseHandle(out_r); CloseHandle(err_r); return; }

    /* Sequential read — safe for typical scripting output sizes */
    g_exec_stdout = read_handle_all(out_r, &g_exec_stdout_len);
    g_exec_stderr = read_handle_all(err_r, &g_exec_stderr_len);
    CloseHandle(out_r);
    CloseHandle(err_r);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    g_exec_code = (int)code;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    g_exec_ok = 1;
}

/* Non-owning peeks: buffers stay owned by the backend (freed by the next
   ls_os_exec_run). LS copies out of them instead of taking ownership, so the
   LS side never frees a backend-malloc'd pointer (memcheck: INVALID FREE). */
void *ls_os_exec_stdout_ptr(void) { return g_exec_stdout; }
void *ls_os_exec_stderr_ptr(void) { return g_exec_stderr; }

void *ls_os_exec_take_stdout(void) {
    void *p = g_exec_stdout; g_exec_stdout = NULL; return p;
}
long long ls_os_exec_stdout_len(void) {
    return (long long)g_exec_stdout_len;
}
void *ls_os_exec_take_stderr(void) {
    void *p = g_exec_stderr; g_exec_stderr = NULL; return p;
}
long long ls_os_exec_stderr_len(void) {
    return (long long)g_exec_stderr_len;
}
int ls_os_exec_get_code(void) { return g_exec_code; }
int ls_os_exec_get_ok(void)   { return g_exec_ok; }

/* =========================================================================
 * Process helpers — popen / pid / exit-code decoding
 * ========================================================================= */

#include <process.h>   /* _getpid */

/* ls_os_popen / ls_os_pread / ls_os_pclose: Win32 pipe via CreateProcess.
 *
 * ls.exe is built with /MT (static CRT).  JIT-resolved symbols like fread
 * come from the DLL CRT (ucrtbase.dll) via the dynamic library search
 * generator.  CRT FILE* objects are not interoperable across CRT instances:
 * a FILE* opened by the static CRT cannot be read by the DLL CRT's fread,
 * causing STATUS_STACK_BUFFER_OVERRUN (0xc0000409).
 *
 * Fix: use Win32 CreateProcess + ReadFile entirely, so no CRT FILE* is
 * involved.  The opaque handle returned to LS is a heap-allocated
 * LsWinPopen struct (pipe read HANDLE + process HANDLE). */

typedef struct {
    HANDLE pipe;    /* readable end of child stdout pipe */
    HANDLE process; /* child process handle              */
} LsWinPopen;

void *ls_os_popen(const char *cmd) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE;

    HANDLE r, w;
    if (!CreatePipe(&r, &w, &sa, 0)) return NULL;
    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = w;
    si.hStdError  = w;  /* merge stderr → stdout so caller sees 2>&1 output */
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    char cmd_buf[32768];
    snprintf(cmd_buf, sizeof(cmd_buf), "cmd.exe /c %s", cmd);

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    BOOL ok = CreateProcessA(NULL, cmd_buf, NULL, NULL,
                             TRUE, 0, NULL, NULL, &si, &pi);
    CloseHandle(w);
    if (!ok) { CloseHandle(r); return NULL; }
    CloseHandle(pi.hThread);

    LsWinPopen *p = (LsWinPopen *)malloc(sizeof(LsWinPopen));
    if (!p) { CloseHandle(r); CloseHandle(pi.hProcess); return NULL; }
    p->pipe    = r;
    p->process = pi.hProcess;
    return p;
}

long long ls_os_pread(void *fp, void *buf, long long maxsz) {
    LsWinPopen *p = (LsWinPopen *)fp;
    DWORD total = 0;
    char *dst = (char *)buf;
    while ((long long)total < maxsz) {
        DWORD n = 0;
        if (!ReadFile(p->pipe, dst + total,
                      (DWORD)(maxsz - (long long)total), &n, NULL) || n == 0)
            break;
        total += n;
    }
    return (long long)total;
}

int ls_os_pclose(void *fp) {
    LsWinPopen *p = (LsWinPopen *)fp;
    CloseHandle(p->pipe);
    WaitForSingleObject(p->process, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(p->process, &code);
    CloseHandle(p->process);
    free(p);
    return (int)code;
}

int ls_os_pid(void) {
    return (int)_getpid();
}

/* On Windows, system() returns the exit code directly (no WEXITSTATUS needed). */
int ls_os_wait_exit_code(int raw) {
    return raw;
}

/* =========================================================================
 * File positioning and deletion — io.ls backend
 * ========================================================================= */

#include <io.h>   /* _unlink */

int ls_os_fseek64(void *fp, long long off, int origin) {
    return _fseeki64((FILE *)fp, (__int64)off, origin);
}

long long ls_os_ftell64(void *fp) {
    return (long long)_ftelli64((FILE *)fp);
}

int ls_os_unlink(const char *path) {
    return _unlink(path);
}

/* =========================================================================
 * Environment variable access and mutation
 *
 * Use Win32 API throughout so that all reads and writes target the same
 * OS-level environment block, regardless of which CRT instance (static /MT
 * vs DLL ucrtbase) the caller uses.  POSIX getenv/setenv read/write the
 * CRT's private copy which is a separate allocation from the Win32 block;
 * mixing them across CRT instances causes set-not-visible-to-get bugs.
 * ========================================================================= */

static char g_getenv_buf[32768];  /* single-buffer, same lifetime as getenv() */

const char *ls_os_getenv(const char *name) {
    DWORD r = GetEnvironmentVariableA(name, g_getenv_buf, (DWORD)sizeof(g_getenv_buf));
    if (r == 0) return NULL;
    return g_getenv_buf;
}

int ls_os_setenv(const char *name, const char *value) {
    return SetEnvironmentVariableA(name, value) ? 0 : -1;
}

int ls_os_unsetenv(const char *name) {
    /* Passing NULL as lpValue removes the variable from the Win32 environment. */
    return SetEnvironmentVariableA(name, NULL) ? 0 : -1;
}

/* =========================================================================
 * Environment snapshot
 * ========================================================================= */

static char **g_env_entries = NULL;
static int    g_env_count   = 0;

void ls_os_env_prepare(void) {
    for (int i = 0; i < g_env_count; i++) free(g_env_entries[i]);
    free(g_env_entries);
    g_env_entries = NULL;
    g_env_count   = 0;

    char *block = GetEnvironmentStringsA();
    if (!block) return;
    int cnt = 0;
    for (char *p = block; *p; p += strlen(p) + 1) cnt++;
    g_env_entries = (char **)malloc((size_t)cnt * sizeof(char *));
    if (!g_env_entries) { FreeEnvironmentStringsA(block); return; }
    int idx = 0;
    for (char *p = block; *p; p += strlen(p) + 1)
        g_env_entries[idx++] = _strdup(p);
    g_env_count = cnt;
    FreeEnvironmentStringsA(block);
}

int ls_os_env_count(void) { return g_env_count; }

const char *ls_os_env_entry(int i) {
    if (i < 0 || i >= g_env_count) return NULL;
    return g_env_entries[i];
}

/* =========================================================================
 * Directory listing
 * ========================================================================= */

static char **g_dir_entries = NULL;
static int    g_dir_count   = 0;
static int    g_dir_cap     = 0;

void ls_os_listdir_prepare(const char *path) {
    int i;
    for (i = 0; i < g_dir_count; i++) free(g_dir_entries[i]);
    free(g_dir_entries);
    g_dir_entries = NULL; g_dir_count = 0; g_dir_cap = 0;

    char pattern[MAX_PATH];
    /* "\\*" — a literal backslash + wildcard. The previous "\*" was an invalid
       C escape that compiled to just "*", so the pattern became "<path>*" and
       FindFirstFileA matched the directory ITSELF instead of its entries
       (list_dir("std") returned ["std"]). */
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        if (g_dir_count >= g_dir_cap) {
            g_dir_cap = g_dir_cap == 0 ? 16 : g_dir_cap * 2;
            g_dir_entries = (char **)realloc(g_dir_entries,
                                             (size_t)g_dir_cap * sizeof(char *));
        }
        g_dir_entries[g_dir_count++] = _strdup(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

int ls_os_listdir_count(void) { return g_dir_count; }

const char *ls_os_listdir_entry(int i) {
    if (i < 0 || i >= g_dir_count) return NULL;
    return g_dir_entries[i];
}

/* =========================================================================
 * Filesystem / path operations
 * ========================================================================= */

static char g_fs_error[512] = "";

static void fs_set_win32_error(const char *prefix) {
    DWORD err = GetLastError();
    char msg[400] = "";
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, 0, msg, (DWORD)sizeof(msg), NULL);
    /* Strip trailing \r\n that FormatMessage appends */
    size_t n = strlen(msg);
    while (n > 0 && (msg[n-1] == '\n' || msg[n-1] == '\r')) msg[--n] = '\0';
    snprintf(g_fs_error, sizeof(g_fs_error), "%s: %s", prefix, msg);
}

const char *ls_os_last_error(void) { return g_fs_error; }

int ls_os_path_exists(const char *path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ? 1 : 0;
}

int ls_os_path_is_dir(const char *path) {
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
}

int ls_os_path_is_file(const char *path) {
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
}

int ls_os_mkdir(const char *path) {
    if (CreateDirectoryA(path, NULL)) return 0;
    fs_set_win32_error("fs.mkdir");
    return -1;
}

int ls_os_mkdir_all(const char *path) {
    char buf[MAX_PATH];
    size_t len = strlen(path);
    if (len == 0 || len >= MAX_PATH) return -1;
    memcpy(buf, path, len + 1);
    /* Replace forward slashes with backslashes */
    for (size_t i = 0; i < len; i++)
        if (buf[i] == '/') buf[i] = '\\';
    /* Walk each component */
    for (size_t i = 1; i <= len; i++) {
        if ((buf[i] == '\\' || buf[i] == '\0') && buf[i-1] != ':') {
            char saved = buf[i];
            buf[i] = '\0';
            if (!CreateDirectoryA(buf, NULL)) {
                DWORD e = GetLastError();
                if (e != ERROR_ALREADY_EXISTS) {
                    fs_set_win32_error("fs.mkdir_all");
                    return -1;
                }
            }
            buf[i] = saved;
        }
    }
    return 0;
}

int ls_os_rmdir(const char *path) {
    if (RemoveDirectoryA(path)) return 0;
    fs_set_win32_error("fs.rmdir");
    return -1;
}

int ls_os_rename_path(const char *from, const char *to) {
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING)) return 0;
    fs_set_win32_error("fs.rename");
    return -1;
}

const char *ls_os_getcwd(void) {
    static char buf[MAX_PATH];
    if (GetCurrentDirectoryA((DWORD)sizeof(buf), buf) == 0) {
        buf[0] = '\0';
        fs_set_win32_error("fs.cwd");
    }
    return buf;
}

int ls_os_chdir(const char *path) {
    if (SetCurrentDirectoryA(path)) return 0;
    fs_set_win32_error("fs.chdir");
    return -1;
}

/* --- High-resolution monotonic clock --- */

long long ls_os_perf_rdtsc(void) {
#if defined(_M_X64) || defined(_M_IX86)
    return (long long)__rdtsc();
#else
    return ls_os_perf_now();
#endif
}

long long ls_os_perf_rdtscp(void) {
#if defined(_M_X64) || defined(_M_IX86)
    unsigned int aux;
    return (long long)__rdtscp(&aux);
#else
    /* Non-x86 fallback: use QPC-based timestamp */
    return ls_os_perf_now();
#endif
}

long long ls_os_perf_now(void) {
    static LARGE_INTEGER freq = { .QuadPart = 0 };
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    /* Scale to nanoseconds using double to avoid 64-bit overflow.
       Typical QPC frequency is ~10 MHz so counter.QuadPart stays well
       below 2^53 for any reasonable uptime. */
    return (long long)((double)counter.QuadPart * 1.0e9
                       / (double)freq.QuadPart);
}

/* =========================================================================
 * Calendar / wall-clock time — std.time backend
 * =========================================================================
 *
 * Windows FILETIME = 100-ns intervals since 1601-01-01.
 * Unix epoch starts 1970-01-01.  Difference = 116444736000000000 * 100ns.
 */

#define EPOCH_DIFF_100NS  116444736000000000LL  /* 100-ns ticks between epochs */

long long ls_os_time_now_unix_ns(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ui;
    ui.LowPart  = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    return (long long)((ui.QuadPart - (ULONGLONG)EPOCH_DIFF_100NS) * 100LL);
}

long long ls_os_time_now_unix_ms(void) {
    return ls_os_time_now_unix_ns() / 1000000LL;
}

/* Internal broken-down time state */
static struct tm g_tm_state;
static int       g_utcoff_s   = 0;   /* seconds east of UTC */

static void fill_state(struct tm *tm_ptr, int utcoff) {
    g_tm_state = *tm_ptr;
    g_utcoff_s = utcoff;
}

static int calc_utcoff(long long unix_s) {
    /* Compute UTC offset by converting unix_s through both localtime and gmtime
       and taking the difference in seconds. */
    time_t t = (time_t)unix_s;
    struct tm loc, utc;
    localtime_s(&loc, &t);
    gmtime_s(&utc, &t);
    /* mktime interprets as local time; _mkgmtime64 as UTC */
    time_t loc_t = mktime(&loc);
    time_t utc_t = _mkgmtime64(&utc);
    return (int)(loc_t - utc_t);
}

void ls_os_time_from_unix_local(long long unix_s) {
    time_t t = (time_t)unix_s;
    struct tm tm_val;
    localtime_s(&tm_val, &t);
    fill_state(&tm_val, calc_utcoff(unix_s));
}

void ls_os_time_from_unix_utc(long long unix_s) {
    time_t t = (time_t)unix_s;
    struct tm tm_val;
    gmtime_s(&tm_val, &t);
    fill_state(&tm_val, 0);
}

int ls_os_time_get_year(void)    { return g_tm_state.tm_year + 1900; }
int ls_os_time_get_month(void)   { return g_tm_state.tm_mon  + 1; }
int ls_os_time_get_day(void)     { return g_tm_state.tm_mday; }
int ls_os_time_get_hour(void)    { return g_tm_state.tm_hour; }
int ls_os_time_get_minute(void)  { return g_tm_state.tm_min; }
int ls_os_time_get_second(void)  { return g_tm_state.tm_sec; }
/* Convert C wday (0=Sun..6=Sat) to LS (0=Mon..6=Sun) */
int ls_os_time_get_weekday(void) { return (g_tm_state.tm_wday + 6) % 7; }
int ls_os_time_get_yday(void)    { return g_tm_state.tm_yday; }
int ls_os_time_get_utcoff(void)  { return g_utcoff_s; }

long long ls_os_time_to_unix(int year, int month, int day,
                              int hour, int minute, int second,
                              int is_utc) {
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_year = year - 1900;
    tm_val.tm_mon  = month - 1;
    tm_val.tm_mday = day;
    tm_val.tm_hour = hour;
    tm_val.tm_min  = minute;
    tm_val.tm_sec  = second;
    tm_val.tm_isdst = -1;
    if (is_utc) return (long long)_mkgmtime64(&tm_val);
    time_t r = mktime(&tm_val);
    return (r == (time_t)-1) ? -1LL : (long long)r;
}

static char g_fmt_buf[512];

const char *ls_os_time_format(int year, int month, int day,
                               int hour, int minute, int second,
                               int weekday, int yday,
                               const char *fmt) {
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_year  = year - 1900;
    tm_val.tm_mon   = month - 1;
    tm_val.tm_mday  = day;
    tm_val.tm_hour  = hour;
    tm_val.tm_min   = minute;
    tm_val.tm_sec   = second;
    tm_val.tm_wday  = (weekday + 1) % 7;  /* LS 0=Mon → C 1=Mon, then %7 */
    tm_val.tm_yday  = yday;
    tm_val.tm_isdst = -1;
    if (!strftime(g_fmt_buf, sizeof(g_fmt_buf), fmt, &tm_val))
        g_fmt_buf[0] = '\0';
    return g_fmt_buf;
}

/* Windows strptime: supports %Y %m %d %H %M %S via sscanf.
   For the subset of formats needed by std.time.parse. */
int ls_os_time_parse(const char *text, const char *fmt) {
    int year=1970, month=1, day=1, hour=0, min=0, sec=0;
    /* Try common date+time formats first, then date-only */
    const char *f = fmt;
    /* Count %Y %m %d %H %M %S fields expected by fmt */
    int y_ok=0, mo_ok=0, d_ok=0, h_ok=0, mi_ok=0, s_ok=0;
    const char *p = f;
    while (*p) {
        if (*p == '%') {
            switch (*(p+1)) {
                case 'Y': y_ok=1;  break;
                case 'm': mo_ok=1; break;
                case 'd': d_ok=1;  break;
                case 'H': h_ok=1;  break;
                case 'M': mi_ok=1; break;
                case 'S': s_ok=1;  break;
            }
        }
        p++;
    }
    /* Build sscanf format by replacing strftime codes */
    char sfmt[256] = "";
    int si = 0;
    p = f;
    while (*p && si < (int)sizeof(sfmt)-3) {
        if (*p == '%') {
            switch (*(p+1)) {
                case 'Y': sfmt[si++]='%'; sfmt[si++]='d'; p+=2; break;
                case 'm': sfmt[si++]='%'; sfmt[si++]='d'; p+=2; break;
                case 'd': sfmt[si++]='%'; sfmt[si++]='d'; p+=2; break;
                case 'H': sfmt[si++]='%'; sfmt[si++]='d'; p+=2; break;
                case 'M': sfmt[si++]='%'; sfmt[si++]='d'; p+=2; break;
                case 'S': sfmt[si++]='%'; sfmt[si++]='d'; p+=2; break;
                default:  sfmt[si++]=*p++; break;
            }
        } else {
            sfmt[si++] = *p++;
        }
    }
    sfmt[si] = '\0';
    int nread = 0;
    int fields = y_ok + mo_ok + d_ok + h_ok + mi_ok + s_ok;
    if (fields == 6)      nread = sscanf(text, sfmt, &year, &month, &day, &hour, &min, &sec);
    else if (fields == 5) nread = sscanf(text, sfmt, &year, &month, &day, &hour, &min);
    else if (fields == 3) nread = sscanf(text, sfmt, &year, &month, &day);
    else                  return 0;
    if (nread < fields) return 0;
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_year  = year - 1900;
    tm_val.tm_mon   = month - 1;
    tm_val.tm_mday  = day;
    tm_val.tm_hour  = hour;
    tm_val.tm_min   = min;
    tm_val.tm_sec   = sec;
    tm_val.tm_isdst = -1;
    fill_state(&tm_val, 0);
    return 1;
}

void ls_os_sleep_ms(long long ms) {
    Sleep((DWORD)(ms < 0 ? 0 : ms));
}

void ls_os_sleep_us(long long us) {
    /* Sleep has ~15ms resolution; for microsecond granularity we'd need
       NtDelayExecution.  Round up to ms — acceptable for v1. */
    long long ms = (us + 999) / 1000;
    Sleep((DWORD)(ms < 0 ? 0 : ms));
}

/* =========================================================================
 * Threads — run an LS `Block()->T` on an OS thread, join for the result.
 * GENERIC over the result type T (std.task's `Task(T)`). The runtime is DUMB:
 * it knows nothing about T. codegen synthesises a per-T `thunk` that calls the
 * closure and stores the by-value result into a `*T box` slot owned by the LS
 * `Task`; the runtime only runs the thunk on a worker and never touches the
 * result bytes (this is the single-owner root for has_drop results — the box
 * is moved out by `join`/`__take`, never memcpy'd by the runtime).
 *
 *   thunk(fn, env, box):  *box = ((T(*)(void*))fn)(env)
 *
 * A Block value is {code_fn, env}; the closure ABI is `T code_fn(void *env)`.
 * The env owns any MOVE-captured heap (Vec/Str/struct/...); its field 0 is the
 * codegen-emitted drop_fn. We run the body, then drop the env exactly ONCE here
 * on the worker — replicating cg_emit_block_env_drop — so move-capture is
 * single-owner across the thread boundary: the spawning scope marked its
 * sources MOVED and will not drop them, and only the worker frees them. CRT
 * malloc/free (ucrt) is thread-safe, so concurrent container alloc/free is fine.
 * _beginthreadex (not CreateThread) so the worker gets CRT per-thread state.
 * ========================================================================= */

typedef void (*ls_task_thunk)(void *fn, void *env, void *box);

typedef struct {
    ls_task_thunk thunk;
    void         *fn;
    void         *env;
    void         *box;      /* result slot, OWNED by the LS Task; runtime never frees it */
    uintptr_t     handle;
} LsThreadCtx;

static unsigned __stdcall ls_thread_trampoline(void *p) {
    LsThreadCtx *c = (LsThreadCtx *)p;
    c->thunk(c->fn, c->env, c->box);            /* run the body; result -> box */
    /* Drop the closure env once, after the body ran (see header). */
    void *env = c->env;
    if (env != NULL) {
        void *drop_fn = *(void **)env;          /* env field 0 = drop_fn */
        if (drop_fn != NULL)
            ((void (*)(void *))drop_fn)(env);
        free(env);
    }
    return 0;
}

/* Spawn: per-T thunk + the closure's raw {code_fn, env} + the result box.
   Returns an opaque handle. The box is owned by the LS Task; we never free it. */
void *ls_thread_spawn(void *thunk, void *fn, void *env, void *box) {
    LsThreadCtx *c = (LsThreadCtx *)malloc(sizeof(LsThreadCtx));
    if (c == NULL) return NULL;
    c->thunk  = (ls_task_thunk)thunk;
    c->fn     = fn;
    c->env    = env;
    c->box    = box;
    c->handle = _beginthreadex(NULL, 0, ls_thread_trampoline, c, 0, NULL);
    return c;
}

/* Join: wait for the worker, free the handle. The result is in the box (LS
   reads it via __take); the box itself is the Task's to free, not ours. */
void ls_thread_join(void *h) {
    LsThreadCtx *c = (LsThreadCtx *)h;
    if (c == NULL) return;
    WaitForSingleObject((HANDLE)c->handle, INFINITE);
    CloseHandle((HANDLE)c->handle);
    free(c);
}

/* =========================================================================
 * Mutexes (std.sync). The OS lock lives behind an opaque heap HANDLE so LS's
 * move semantics never relocate it (a moved pthread_mutex_t / SRWLOCK is UB).
 * Windows: a heap SRWLOCK — pointer-sized, faster and smaller than
 * CRITICAL_SECTION, and it needs NO destroy call (we just free the block).
 * Non-recursive: re-locking on the same thread deadlocks (documented). lock/
 * unlock/trylock are thin pass-throughs; destroy is null-safe so a Mutex built
 * `= {}` and never init'd still drops cleanly.
 * ========================================================================= */

void *ls_mutex_init(void) {
    SRWLOCK *m = (SRWLOCK *)malloc(sizeof(SRWLOCK));
    if (m == NULL) return NULL;
    InitializeSRWLock(m);
    return m;
}

int ls_mutex_lock(void *h) {
    if (h == NULL) return -1;
    AcquireSRWLockExclusive((SRWLOCK *)h);
    return 0;
}

int ls_mutex_trylock(void *h) {
    if (h == NULL) return 0;
    return TryAcquireSRWLockExclusive((SRWLOCK *)h) ? 1 : 0;
}

int ls_mutex_unlock(void *h) {
    if (h == NULL) return -1;
    ReleaseSRWLockExclusive((SRWLOCK *)h);
    return 0;
}

void ls_mutex_destroy(void *h) {
    if (h != NULL) free(h);   /* SRWLOCK has no destroy function */
}

/* Spin-wait CPU hint (SpinLock). YieldProcessor() is `pause` on x64 / `yield`
 * on ARM64 — relaxes the pipeline and eases cache-line contention WITHOUT
 * yielding the core (the whole point of a spinlock vs a mutex). */
void ls_cpu_relax(void) {
    YieldProcessor();
}

/* Yield the core to another ready thread when a spin has run too long. Bounds
 * CPU burn under contention AND breaks priority inversion: a LOW-priority lock
 * holder can be scheduled to release the lock instead of a high-priority spinner
 * starving it forever. SwitchToThread yields to any ready thread on this
 * processor INCLUDING lower-priority ones (unlike Sleep(0), which only yields to
 * equal/higher priority — useless against inversion). */
void ls_cpu_yield(void) {
    SwitchToThread();
}
