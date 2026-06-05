# alloc.py — Python reference for the LS alloc benchmark.
#   python alloc.py [n] [iters]
import sys
import time


def vec_stress(n):
    v = []
    for i in range(n):
        v.append("item_" + str(i))
    chk = 0
    for s in v:
        chk += len(s)
    return chk


def map_stress(n):
    freq = {}
    keyspace = 8192
    for i in range(n):
        key = "key_" + str(i % keyspace)
        cur = freq.get(key, 0)
        freq[key] = cur + 1
    return len(freq)


def main():
    n = int(sys.argv[1]) if len(sys.argv) >= 2 else 200000
    iters = int(sys.argv[2]) if len(sys.argv) >= 3 else 5

    _warm = vec_stress(n) + map_stress(n)

    total_ns = 0
    chk = 0
    for _ in range(iters):
        t0 = time.perf_counter_ns()
        chk += vec_stress(n) + map_stress(n)
        total_ns += time.perf_counter_ns() - t0
    mean_ns = total_ns / iters
    print(f"result: {chk}")
    print(f"[@bench] mean {mean_ns:.1f} ns ({iters} iterations)")


main()
