/* string.c — C benchmark: character iteration over a long string (MSVC)
 * Uses volatile function pointer to prevent call elimination by optimizer.
 * Usage: string_c.exe [n], default n=200000
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

int count_char(const char *s, char c, int end) {
    int count = 0;
    for (int i = 0; i < end; i++) {
        if (s[i] == c) count++;
    }
    return count;
}

int main(int argc, char *argv[]) {
    int n = 200000;
    if (argc > 1) {
        n = atoi(argv[1]);
        if (n <= 0) n = 200000;
    }
    int iters = 10;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    const char *pattern = "a b c d e f g ";
    int plen = (int)strlen(pattern);
    int repeats = n / plen + 1;
    int total_len = repeats * plen;

    char *s = (char*)malloc(total_len + 1);
    for (int i = 0; i < repeats; i++) {
        memcpy(s + i * plen, pattern, plen);
    }
    s[total_len] = '\0';

    /* volatile function pointer prevents optimizer from eliminating the call */
    int (*volatile count_ptr)(const char*, char, int) = count_char;

    /* warm-up */
    count_ptr(s, ' ', total_len);

    int result = 0;
    LARGE_INTEGER total;
    total.QuadPart = 0;

    for (int i = 0; i < iters; i++) {
        int end = total_len;
        /* opaque: count_ptr depends on end which is set via normal code path */
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        result += count_ptr(s, ' ', end);
        QueryPerformanceCounter(&t1);
        total.QuadPart += t1.QuadPart - t0.QuadPart;
    }

    double mean_ns = (double)total.QuadPart * 1e9 / (freq.QuadPart * iters);
    printf("result: %d\n", result);
    printf("[@bench] mean %.1f ns (%d iterations)\n", mean_ns, iters);

    free(s);
    return 0;
}
