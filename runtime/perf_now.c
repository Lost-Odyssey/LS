/* perf_now.c — Minimal standalone ls_os_perf_now for the ls_profiler static lib.
 *
 * This file is compiled ONLY into ls_profiler.lib (AOT --profile linking).
 * The JIT path uses the implementation in jit.c; ls.exe itself uses the one
 * in os_win32.c / os_posix.c (compiled into LS_SOURCES).
 */

#include <stdint.h>

#ifdef _WIN32
/* Avoid pulling in <windows.h> to prevent TokenType collision. */
typedef union { long long QuadPart; } LARGE_INTEGER_T;
__declspec(dllimport) int __stdcall QueryPerformanceCounter(LARGE_INTEGER_T *);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(LARGE_INTEGER_T *);

long long ls_os_perf_now(void) {
    static LARGE_INTEGER_T freq = { .QuadPart = 0 };
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    LARGE_INTEGER_T counter;
    QueryPerformanceCounter(&counter);
    return (long long)((double)counter.QuadPart * 1.0e9
                       / (double)freq.QuadPart);
}

#else
#include <time.h>

long long ls_os_perf_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}
#endif
