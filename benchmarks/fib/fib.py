"""Python benchmark: naive recursive Fibonacci
Usage: python fib.py [n] [iters], defaults n=35 iters=10
"""
import time
import sys
sys.setrecursionlimit(1000000)


def fib(n: int) -> int:
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)


def main() -> None:
    n = 35
    if len(sys.argv) > 1:
        n = int(sys.argv[1])
        if n <= 0 or n > 45:
            n = 35
    iters = 10
    if len(sys.argv) > 2:
        iters = int(sys.argv[2])

    # warm-up
    fib(n)

    result = 0
    total_ns = 0

    for _ in range(iters):
        t0 = time.perf_counter_ns()
        result += fib(n)
        t1 = time.perf_counter_ns()
        total_ns += t1 - t0

    mean_ns = total_ns / iters
    print(f"result: {result}")
    print(f"[@bench] mean {mean_ns:.1f} ns ({iters} iterations)")


if __name__ == "__main__":
    main()
