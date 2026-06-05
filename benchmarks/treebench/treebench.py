# treebench.py — Python reference: tree traversal (tuples, by reference).
#   python treebench.py [depth]
import sys
import time

sys.setrecursionlimit(1000000)


def build(depth, v):
    if depth == 0:
        return (v,)            # leaf: 1-tuple
    return (build(depth - 1, v * 2), build(depth - 1, v * 2 + 1))  # node: 2-tuple


def sum_tree(t):
    if len(t) == 1:
        return t[0]
    return sum_tree(t[0]) + sum_tree(t[1])


def main():
    depth = int(sys.argv[1]) if len(sys.argv) >= 2 else 16
    iters = 5
    tr = build(depth, 1)
    s = 0
    t0 = time.perf_counter_ns()
    for _ in range(iters):
        s = sum_tree(tr)
    t1 = time.perf_counter_ns()
    mean = (t1 - t0) / iters / 1000.0
    print(f"result: {s}")
    print(f"[@bench] mean {mean:.1f} us (depth={depth}, {iters} sum)")


main()
