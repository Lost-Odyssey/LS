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

/* =========================================================================
 * Threads — run an LS `Block()->T` on an OS thread, join for the result.
 * POSIX counterpart of the os_win32.c section (see there for the full model).
 * GENERIC over T: the runtime is DUMB — codegen synthesises a per-T `thunk`
 *   thunk(fn, env, box):  *box = ((T(*)(void*))fn)(env)
 * that stores the closure's by-value result into a `*T` box the LS Task owns;
 * the runtime only runs the thunk on a worker and NEVER touches the result
 * bytes (single-owner root for has_drop results — join() moves it out via
 * __take). The env owns any MOVE-captured heap; its field 0 is the codegen-
 * emitted drop_fn, which we run once on the worker after the body, then free
 * the env — single-owner across the thread boundary. glibc/musl malloc/free is
 * thread-safe, so concurrent container alloc/free is fine.
 * ========================================================================= */

#include <pthread.h>

typedef void (*ls_task_thunk)(void *fn, void *env, void *box);

typedef struct {
    ls_task_thunk thunk;
    void         *fn;
    void         *env;
    void         *box;      /* result slot, OWNED by the LS Task; runtime never frees it */
    pthread_t     handle;
} LsThreadCtx;

static void *ls_thread_trampoline(void *p) {
    LsThreadCtx *c = (LsThreadCtx *)p;
    c->thunk(c->fn, c->env, c->box);            /* run the body; result -> box */
    /* L-015: the closure env is dropped+freed INSIDE the thunk now (LS-emitted,
       memcheck-tracked free), not here. The trampoline used to free env with a
       raw free() that the leak tracker never saw → false per-spawn leak. */
    return NULL;
}

/* Spawn: per-T thunk + the closure's raw {code_fn, env} + the result box.
   Returns an opaque handle. The box is owned by the LS Task; we never free it. */
void *ls_thread_spawn(void *thunk, void *fn, void *env, void *box) {
    LsThreadCtx *c = (LsThreadCtx *)malloc(sizeof(LsThreadCtx));
    if (c == NULL) return NULL;
    c->thunk = (ls_task_thunk)thunk;
    c->fn    = fn;
    c->env   = env;
    c->box   = box;
    if (pthread_create(&c->handle, NULL, ls_thread_trampoline, c) != 0) {
        free(c);
        return NULL;
    }
    return c;
}

/* Join: wait for the worker, free the handle. The result is in the box (LS
   reads it via __take); the box itself is the Task's to free, not ours. */
void ls_thread_join(void *h) {
    LsThreadCtx *c = (LsThreadCtx *)h;
    if (c == NULL) return;
    pthread_join(c->handle, NULL);
    free(c);
}

/* =========================================================================
 * Mutexes (std.sync) — a heap pthread_mutex_t behind an opaque handle so LS's
 * move semantics never relocate it (a moved pthread_mutex_t is UB). lock/unlock/
 * trylock pass through; destroy is null-safe (a Mutex built `= {}` and never
 * init'd drops cleanly). Non-recursive default (re-lock on same thread = UB).
 * ========================================================================= */

#include <sched.h>

void *ls_mutex_init(void) {
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (m == NULL) return NULL;
    if (pthread_mutex_init(m, NULL) != 0) { free(m); return NULL; }
    return m;
}

int ls_mutex_lock(void *h) {
    if (h == NULL) return -1;
    return pthread_mutex_lock((pthread_mutex_t *)h);
}

int ls_mutex_trylock(void *h) {
    if (h == NULL) return 0;
    return pthread_mutex_trylock((pthread_mutex_t *)h) == 0 ? 1 : 0;
}

int ls_mutex_unlock(void *h) {
    if (h == NULL) return -1;
    return pthread_mutex_unlock((pthread_mutex_t *)h);
}

void ls_mutex_destroy(void *h) {
    if (h != NULL) {
        pthread_mutex_destroy((pthread_mutex_t *)h);
        free(h);
    }
}

/* Reader-writer lock (RwLock) — a heap pthread_rwlock_t acquired in SHARED
 * (read) or EXCLUSIVE (write) mode: many readers OR one writer. Same heap-handle/
 * move/null-safe-destroy shape as the mutex above. Both rd/wr unlock map to the
 * single pthread_rwlock_unlock (kept as two LS names for symmetry with the API). */
void *ls_rwlock_init(void) {
    pthread_rwlock_t *m = (pthread_rwlock_t *)malloc(sizeof(pthread_rwlock_t));
    if (m == NULL) return NULL;
    if (pthread_rwlock_init(m, NULL) != 0) { free(m); return NULL; }
    return m;
}

int ls_rwlock_rdlock(void *h) {
    if (h == NULL) return -1;
    return pthread_rwlock_rdlock((pthread_rwlock_t *)h);
}

int ls_rwlock_wrlock(void *h) {
    if (h == NULL) return -1;
    return pthread_rwlock_wrlock((pthread_rwlock_t *)h);
}

int ls_rwlock_rdunlock(void *h) {
    if (h == NULL) return -1;
    return pthread_rwlock_unlock((pthread_rwlock_t *)h);
}

int ls_rwlock_wrunlock(void *h) {
    if (h == NULL) return -1;
    return pthread_rwlock_unlock((pthread_rwlock_t *)h);
}

void ls_rwlock_destroy(void *h) {
    if (h != NULL) {
        pthread_rwlock_destroy((pthread_rwlock_t *)h);
        free(h);
    }
}

/* Condition variable (std.chan blocking Chan). A heap pthread_cond_t behind an
 * opaque handle (same move-safe shape as the mutex). It pairs with a
 * pthread_mutex_t handle: ls_cond_wait atomically releases the mutex and sleeps
 * until signalled, then re-acquires it before returning. Lost-wakeup safety is
 * the CALLER's job: always wait under the mutex inside a `while (predicate)`. */
void *ls_cond_init(void) {
    pthread_cond_t *cv = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
    if (cv == NULL) return NULL;
    if (pthread_cond_init(cv, NULL) != 0) { free(cv); return NULL; }
    return cv;
}

void ls_cond_wait(void *h_cond, void *h_mtx) {
    if (h_cond == NULL || h_mtx == NULL) return;
    pthread_cond_wait((pthread_cond_t *)h_cond, (pthread_mutex_t *)h_mtx);
}

void ls_cond_signal(void *h) {
    if (h != NULL) pthread_cond_signal((pthread_cond_t *)h);
}

void ls_cond_broadcast(void *h) {
    if (h != NULL) pthread_cond_broadcast((pthread_cond_t *)h);
}

void ls_cond_destroy(void *h) {
    if (h != NULL) {
        pthread_cond_destroy((pthread_cond_t *)h);
        free(h);
    }
}

/* Spin-wait CPU hint (SpinLock) — relax the pipeline without yielding the core. */
void ls_cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield");
#else
    sched_yield();
#endif
}

/* Yield the core when a spin has run too long — bounds CPU burn under contention
 * and breaks priority inversion (lets a low-priority lock holder be scheduled to
 * release). sched_yield moves the caller to the end of its priority run-queue. */
void ls_cpu_yield(void) {
    sched_yield();
}

/* Number of logical processors — par_for's default worker fan-out. */
int __ls_cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

/* Cache size in KB for `level` (1=L1d, 2=L2, 3=L3). Best-effort via sysconf
 * (glibc extension); 0 if unavailable so callers fall back to a portable
 * default. Used by std.sci.nn.sgemm_packed for analytical cache blocking. */
int __ls_cache_kb(int level) {
    long sz = 0;
#if defined(_SC_LEVEL1_DCACHE_SIZE)
    if (level == 1)      sz = sysconf(_SC_LEVEL1_DCACHE_SIZE);
    else if (level == 2) sz = sysconf(_SC_LEVEL2_CACHE_SIZE);
    else if (level == 3) sz = sysconf(_SC_LEVEL3_CACHE_SIZE);
#else
    (void)level;
#endif
    return sz > 0 ? (int)(sz / 1024) : 0;
}

/* 1 if the host supports AVX-512 Foundation, else 0. std.sci.nn.sgemm uses this
 * to pick the 12x32 (AVX-512) vs 6x16 (AVX2) micro-kernel. */
int __ls_cpu_has_avx512(void) {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx512f") ? 1 : 0;
#else
    return 0;
#endif
}
