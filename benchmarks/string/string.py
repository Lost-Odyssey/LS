"""Python benchmark: character iteration over a long string
Usage: python string.py [n] [iters], defaults n=200000 iters=10
"""
import time
import sys

def count_char(s: str, c: str, end: int) -> int:
    count = 0
    for i in range(end):
        if s[i] == c:
            count += 1
    return count

def main() -> None:
    n = 200000
    if len(sys.argv) > 1:
        n = int(sys.argv[1])
        if n <= 0:
            n = 200000
    iters = 10
    if len(sys.argv) > 2:
        iters = int(sys.argv[2])

    pattern = "a b c d e f g "
    plen = len(pattern)  # 13
    repeats = n // plen + 1
    s = pattern * repeats

    # warm-up
    count_char(s, ' ', len(s))

    result = 0
    total_ns = 0

    for _ in range(iters):
        end = len(s)
        t0 = time.perf_counter_ns()
        result += count_char(s, ' ', end)
        t1 = time.perf_counter_ns()
        total_ns += t1 - t0

    mean_ns = total_ns / iters
    print(f"result: {result}")
    print(f"[@bench] mean {mean_ns:.1f} ns ({iters} iterations)")

if __name__ == "__main__":
    main()
