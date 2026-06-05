// structbench.cpp — C++ (MSVC) reference for struct/enum benchmark.
//   cl /O2 /std:c++17 /EHsc /MT structbench.cpp /Fe:structbench_cpp.exe
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <chrono>

struct Point {
    double x, y;
    double dist2() const { return x * x + y * y; }
};
struct Circle {
    Point center;
    double r;
    double area() const { return 3.14159 * r * r; }
};

// tagged union, mirrors LS enum Shape
struct Shape {
    int tag; // 0=Dot 1=Seg 2=Box
    double a, b;
};
static double shape_measure(const Shape &s) {
    switch (s.tag) {
        case 0: return 0.0;
        case 1: return s.a;
        case 2: return s.a * s.b;
    }
    return 0.0;
}

// prevent the optimizer from eliding the loops
static volatile double sink;

static double bench_scalar(long long n) {
    double s = 0.0;
    for (long long i = 0; i < n; i++) {
        double c = (double)(i % 1000);
        Point p { c, c * 2.0 };
        s += p.dist2();
    }
    return s;
}
static double bench_nested(long long n) {
    double s = 0.0;
    for (long long i = 0; i < n; i++) {
        double c = (double)(i % 1000);
        Circle cir { Point { c, 0.0 }, c };
        s += cir.area() + cir.center.x;
    }
    return s;
}
static double bench_enum(long long n) {
    double s = 0.0;
    for (long long i = 0; i < n; i++) {
        double c = (double)(i % 1000);
        int k = (int)(i % 3);
        Shape sh { 0, 0, 0 };
        if (k == 1) sh = Shape { 1, c, 0 };
        if (k == 2) sh = Shape { 2, 2.0, c };
        s += shape_measure(sh);
    }
    return s;
}

int main(int argc, char **argv) {
    long long n = (argc >= 2) ? atoll(argv[1]) : 1000000;
    sink = bench_scalar(1000) + bench_nested(1000) + bench_enum(1000);

    auto t0 = std::chrono::high_resolution_clock::now();
    double r1 = bench_scalar(n); sink = r1;
    auto t1 = std::chrono::high_resolution_clock::now();
    double r2 = bench_nested(n); sink = r2;
    auto t2 = std::chrono::high_resolution_clock::now();
    double r3 = bench_enum(n); sink = r3;
    auto t3 = std::chrono::high_resolution_clock::now();

    auto us = [](auto a, auto b) {
        return (long long)std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
    };
    long long chk = (long long)(r1 + r2 + r3);
    printf("result: %lld\n", chk);
    printf("[@bench] scalar    %lld us\n", us(t0, t1));
    printf("[@bench] nested    %lld us\n", us(t1, t2));
    printf("[@bench] enum      %lld us\n", us(t2, t3));
    printf("[@bench] mean %lld us (%lld iterations)\n", us(t0, t3), n);
    return 0;
}
