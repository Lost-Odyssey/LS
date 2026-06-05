/* fib.c — C benchmark: naive recursive Fibonacci (MSVC)
 * Uses volatile function pointer to prevent call elimination by optimizer.
 * Usage: fib_c.exe [n], default n=35
 */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(int argc, char *argv[]) {
    int n = 35;
    if (argc > 1) {
        n = atoi(argv[1]);
        if (n <= 0 || n > 45) n = 35;
    }
    int iters = 10;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    /* volatile function pointer prevents optimizer from eliminating the call */
    int (*volatile fib_ptr)(int) = fib;

    /* warm-up */
    fib_ptr(n);

    int result = 0;
    LARGE_INTEGER total;
    total.QuadPart = 0;

    for (int i = 0; i < iters; i++) {
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        result += fib_ptr(n);
        QueryPerformanceCounter(&t1);
        total.QuadPart += t1.QuadPart - t0.QuadPart;
    }

    double mean_ns = (double)total.QuadPart * 1e9 / (freq.QuadPart * iters);
    printf("result: %d\n", result);
    printf("[@bench] mean %.1f ns (%d iterations)\n", mean_ns, iters);
    return 0;
}
