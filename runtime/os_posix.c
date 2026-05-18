/* os_posix.c — POSIX OS backend for LS runtime (Linux / macOS).
 *
 * This is the ONLY file in the project that uses POSIX process/environment
 * APIs (fork, pipe, waitpid, environ).  All other LS runtime/stdlib files
 * call the ls_os_* symbols declared in os_backend.h.
 *
 * Compiled exclusively on non-Windows platforms (CMakeLists routes based
 * on platform).
 */

#define _GNU_SOURCE   /* for strptime on Linux */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include "os_backend.h"

/* =========================================================================
 * Process execution — exec_full backend
 * ========================================================================= */

static char  *g_exec_stdout     = NULL;
static size_t g_exec_stdout_len = 0;
static char  *g_exec_stderr     = NULL;
static size_t g_exec_stderr_len = 0;
static int    g_exec_code       = 0;
static int    g_exec_ok         = 0;

static char *read_fd_all(int fd, size_t *out_len) {
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { *out_len = 0; return NULL; }
    for (;;) {
        ssize_t n = read(fd, buf + len, cap - len - 1);
        if (n <= 0) break;
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

    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0) return;
    if (pipe(err_pipe) != 0) { close(out_pipe[0]); close(out_pipe[1]); return; }

    pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        return;
    }
    if (pid == 0) {
        /* child */
        close(out_pipe[0]); close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]); close(err_pipe[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    /* parent: close write ends */
    close(out_pipe[1]);
    close(err_pipe[1]);

    /* Sequential read (v1 — may deadlock if child fills stderr while
       parent is still reading stdout; sufficient for typical scripts) */
    g_exec_stdout = read_fd_all(out_pipe[0], &g_exec_stdout_len);
    g_exec_stderr = read_fd_all(err_pipe[0], &g_exec_stderr_len);
    close(out_pipe[0]);
    close(err_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    g_exec_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    g_exec_ok   = 1;
}

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

#include <stdio.h>

void *ls_os_popen(const char *cmd) {
    return (void *)popen(cmd, "r");
}

long long ls_os_pread(void *fp, void *buf, long long maxsz) {
    return (long long)fread(buf, 1, (size_t)maxsz, (FILE *)fp);
}

int ls_os_pclose(void *fp) {
    int raw = pclose((FILE *)fp);
    return WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
}

int ls_os_pid(void) {
    return (int)getpid();
}

/* On POSIX, system() and pclose() return a wait(2) status word.
   The actual exit code is extracted via WEXITSTATUS. */
int ls_os_wait_exit_code(int raw) {
    return (raw >> 8) & 0xff;
}

/* =========================================================================
 * File positioning and deletion — io.ls backend
 * ========================================================================= */

int ls_os_fseek64(void *fp, long long off, int origin) {
    return fseeko((FILE *)fp, (off_t)off, origin);
}

long long ls_os_ftell64(void *fp) {
    return (long long)ftello((FILE *)fp);
}

int ls_os_unlink(const char *path) {
    return unlink(path);
}

/* =========================================================================
 * Environment variable mutation
 * ========================================================================= */

const char *ls_os_getenv(const char *name) {
    return getenv(name);
}

int ls_os_setenv(const char *name, const char *value) {
    return setenv(name, value, 1);
}

int ls_os_unsetenv(const char *name) {
    return unsetenv(name);
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

    extern char **environ;
    int cnt = 0;
    while (environ[cnt]) cnt++;
    g_env_entries = (char **)malloc((size_t)cnt * sizeof(char *));
    if (!g_env_entries) return;
    for (int i = 0; i < cnt; i++)
        g_env_entries[i] = strdup(environ[i]);
    g_env_count = cnt;
}

int ls_os_env_count(void) { return g_env_count; }

const char *ls_os_env_entry(int i) {
    if (i < 0 || i >= g_env_count) return NULL;
    return g_env_entries[i];
}

/* =========================================================================
 * Directory listing
 * ========================================================================= */
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>

static char **g_dir_entries = NULL;
static int    g_dir_count   = 0;
static int    g_dir_cap     = 0;

void ls_os_listdir_prepare(const char *path) {
    int i;
    for (i = 0; i < g_dir_count; i++) free(g_dir_entries[i]);
    free(g_dir_entries);
    g_dir_entries = NULL; g_dir_count = 0; g_dir_cap = 0;

    DIR *d = opendir(path);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (g_dir_count >= g_dir_cap) {
            g_dir_cap = g_dir_cap == 0 ? 16 : g_dir_cap * 2;
            g_dir_entries = (char **)realloc(g_dir_entries,
                                             (size_t)g_dir_cap * sizeof(char *));
        }
        g_dir_entries[g_dir_count++] = strdup(de->d_name);
    }
    closedir(d);
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

static void fs_set_errno_error(const char *prefix) {
    snprintf(g_fs_error, sizeof(g_fs_error), "%s: %s", prefix, strerror(errno));
}

const char *ls_os_last_error(void) { return g_fs_error; }

int ls_os_path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? 1 : 0;
}

int ls_os_path_is_dir(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
}

int ls_os_path_is_file(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode)) ? 1 : 0;
}

int ls_os_mkdir(const char *path) {
    if (mkdir(path, 0755) == 0) return 0;
    fs_set_errno_error("fs.mkdir");
    return -1;
}

int ls_os_mkdir_all(const char *path) {
    char buf[4096];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(buf)) return -1;
    memcpy(buf, path, len + 1);
    for (size_t i = 1; i <= len; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char saved = buf[i];
            buf[i] = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
                fs_set_errno_error("fs.mkdir_all");
                return -1;
            }
            buf[i] = saved;
        }
    }
    return 0;
}

int ls_os_rmdir(const char *path) {
    if (rmdir(path) == 0) return 0;
    fs_set_errno_error("fs.rmdir");
    return -1;
}

int ls_os_rename_path(const char *from, const char *to) {
    if (rename(from, to) == 0) return 0;
    fs_set_errno_error("fs.rename");
    return -1;
}

const char *ls_os_getcwd(void) {
    static char buf[4096];
    if (getcwd(buf, sizeof(buf)) == NULL) {
        buf[0] = '\0';
        fs_set_errno_error("fs.cwd");
    }
    return buf;
}

int ls_os_chdir(const char *path) {
    if (chdir(path) == 0) return 0;
    fs_set_errno_error("fs.chdir");
    return -1;
}

/* --- High-resolution monotonic clock --- */

long long ls_os_perf_rdtsc(void) {
#if defined(__x86_64__) || defined(__i386__)
    return (long long)__builtin_ia32_rdtsc();
#else
    return ls_os_perf_now();
#endif
}

long long ls_os_perf_rdtscp(void) {
#if defined(__x86_64__) || defined(__i386__)
    unsigned int aux;
    return (long long)__builtin_ia32_rdtscp(&aux);
#else
    return ls_os_perf_now();
#endif
}

long long ls_os_perf_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

/* =========================================================================
 * Calendar / wall-clock time — std.time backend
 * ========================================================================= */

long long ls_os_time_now_unix_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

long long ls_os_time_now_unix_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000LL);
}

/* Internal state */
static struct tm g_tm_state;
static int       g_utcoff_s = 0;

void ls_os_time_from_unix_local(long long unix_s) {
    time_t t = (time_t)unix_s;
    localtime_r(&t, &g_tm_state);
    g_utcoff_s = (int)g_tm_state.tm_gmtoff;
}

void ls_os_time_from_unix_utc(long long unix_s) {
    time_t t = (time_t)unix_s;
    gmtime_r(&t, &g_tm_state);
    g_utcoff_s = 0;
}

int ls_os_time_get_year(void)    { return g_tm_state.tm_year + 1900; }
int ls_os_time_get_month(void)   { return g_tm_state.tm_mon  + 1; }
int ls_os_time_get_day(void)     { return g_tm_state.tm_mday; }
int ls_os_time_get_hour(void)    { return g_tm_state.tm_hour; }
int ls_os_time_get_minute(void)  { return g_tm_state.tm_min; }
int ls_os_time_get_second(void)  { return g_tm_state.tm_sec; }
int ls_os_time_get_weekday(void) { return (g_tm_state.tm_wday + 6) % 7; }
int ls_os_time_get_yday(void)    { return g_tm_state.tm_yday; }
int ls_os_time_get_utcoff(void)  { return g_utcoff_s; }

long long ls_os_time_to_unix(int year, int month, int day,
                              int hour, int minute, int second,
                              int is_utc) {
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_year  = year - 1900;
    tm_val.tm_mon   = month - 1;
    tm_val.tm_mday  = day;
    tm_val.tm_hour  = hour;
    tm_val.tm_min   = minute;
    tm_val.tm_sec   = second;
    tm_val.tm_isdst = -1;
    if (is_utc) return (long long)timegm(&tm_val);
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
    tm_val.tm_wday  = (weekday + 1) % 7;
    tm_val.tm_yday  = yday;
    tm_val.tm_isdst = -1;
    if (!strftime(g_fmt_buf, sizeof(g_fmt_buf), fmt, &tm_val))
        g_fmt_buf[0] = '\0';
    return g_fmt_buf;
}

int ls_os_time_parse(const char *text, const char *fmt) {
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));
    char *end = strptime(text, fmt, &tm_val);
    if (!end) return 0;
    g_tm_state = tm_val;
    g_utcoff_s = 0;
    return 1;
}

void ls_os_sleep_ms(long long ms) {
    if (ms <= 0) return;
    struct timespec req;
    req.tv_sec  = (time_t)(ms / 1000);
    req.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&req, NULL);
}

void ls_os_sleep_us(long long us) {
    if (us <= 0) return;
    struct timespec req;
    req.tv_sec  = (time_t)(us / 1000000LL);
    req.tv_nsec = (long)((us % 1000000LL) * 1000L);
    nanosleep(&req, NULL);
}
