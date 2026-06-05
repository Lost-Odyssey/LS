# strbench.py — Python str reference.
#   python strbench.py [n]
import sys
import time


def main():
    n = int(sys.argv[1]) if len(sys.argv) >= 2 else 1000000
    base = "The Quick Brown Fox Jumps Over The Lazy Dog"
    needles = ["Fox", "Dog", "The", "Lazy", "Quick"]

    t0 = time.perf_counter_ns()
    a1 = 0
    for _ in range(n):
        u = base.upper()
        a1 += len(u)
    t1 = time.perf_counter_ns()

    a2 = 0
    for i in range(n):
        nd = needles[i % 5]
        if nd in base:
            a2 += 1
    t2 = time.perf_counter_ns()

    a3 = 0
    for _ in range(n):
        parts = base.split(" ")
        a3 += len(parts)
    t3 = time.perf_counter_ns()

    a4 = 0
    for _ in range(n):
        r = base.replace("o", "0")
        a4 += len(r)
    t4 = time.perf_counter_ns()

    a5 = 0
    for i in range(n):
        p = i % 10
        s = base[p:p+5]
        a5 += ord(s[0])
    t5 = time.perf_counter_ns()

    chk = a1 + a2 + a3 + a4 + a5
    print(f"result: {chk}")
    print(f"[@bench] upper     {(t1 - t0) // 1000} us")
    print(f"[@bench] contains  {(t2 - t1) // 1000} us")
    print(f"[@bench] split     {(t3 - t2) // 1000} us")
    print(f"[@bench] replace   {(t4 - t3) // 1000} us")
    print(f"[@bench] substr    {(t5 - t4) // 1000} us")
    print(f"[@bench] mean {(t5 - t0) // 1000} us ({n} iterations)")


main()
