# structbench.py — Python reference for struct/enum benchmark.
#   python structbench.py [n]
import sys
import time


class Point:
    __slots__ = ("x", "y")

    def __init__(self, x, y):
        self.x = x
        self.y = y

    def dist2(self):
        return self.x * self.x + self.y * self.y


class Circle:
    __slots__ = ("center", "r")

    def __init__(self, center, r):
        self.center = center
        self.r = r

    def area(self):
        return 3.14159 * self.r * self.r


# tagged tuple, mirrors LS enum (tag, a, b)
def shape_measure(s):
    tag = s[0]
    if tag == 0:
        return 0.0
    elif tag == 1:
        return s[1]
    else:
        return s[1] * s[2]


def bench_scalar(n):
    s = 0.0
    for i in range(n):
        c = float(i % 1000)
        p = Point(c, c * 2.0)
        s += p.dist2()
    return s


def bench_nested(n):
    s = 0.0
    for i in range(n):
        c = float(i % 1000)
        cir = Circle(Point(c, 0.0), c)
        s += cir.area() + cir.center.x
    return s


def bench_enum(n):
    s = 0.0
    for i in range(n):
        c = float(i % 1000)
        k = i % 3
        sh = (0, 0.0, 0.0)
        if k == 1:
            sh = (1, c, 0.0)
        if k == 2:
            sh = (2, 2.0, c)
        s += shape_measure(sh)
    return s


def main():
    n = int(sys.argv[1]) if len(sys.argv) >= 2 else 1000000

    _w = bench_scalar(1000) + bench_nested(1000) + bench_enum(1000)

    t0 = time.perf_counter_ns()
    r1 = bench_scalar(n)
    t1 = time.perf_counter_ns()
    r2 = bench_nested(n)
    t2 = time.perf_counter_ns()
    r3 = bench_enum(n)
    t3 = time.perf_counter_ns()

    chk = int(r1 + r2 + r3)
    print(f"result: {chk}")
    print(f"[@bench] scalar    {(t1 - t0) // 1000} us")
    print(f"[@bench] nested    {(t2 - t1) // 1000} us")
    print(f"[@bench] enum      {(t3 - t2) // 1000} us")
    print(f"[@bench] mean {(t3 - t0) // 1000} us ({n} iterations)")


main()
