# vecbench.py — Python list reference.
#   python vecbench.py [n]
import sys
import time


def main():
    n = int(sys.argv[1]) if len(sys.argv) >= 2 else 10000000

    t0 = time.perf_counter_ns()
    v = []
    for i in range(n):
        v.append(i % 1000)
    t1 = time.perf_counter_ns()

    sum1 = 0
    for i in range(n):
        sum1 += v[i]
    t2 = time.perf_counter_ns()

    sum2 = 0
    for x in v:
        sum2 += x
    t3 = time.perf_counter_ns()

    for i in range(n):
        v[i] = v[i] + 1
    t4 = time.perf_counter_ns()

    chk = sum1 + sum2 + v[0] + v[n - 1]
    print(f"result: {chk}")
    print(f"[@bench] push      {(t1 - t0) // 1000} us")
    print(f"[@bench] index_r   {(t2 - t1) // 1000} us")
    print(f"[@bench] for_in    {(t3 - t2) // 1000} us")
    print(f"[@bench] index_w   {(t4 - t3) // 1000} us")
    print(f"[@bench] mean {(t4 - t0) // 1000} us ({n} iterations)")


main()
