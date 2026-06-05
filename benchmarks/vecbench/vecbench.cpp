// vecbench.cpp — C++ std::vector reference.
//   cl /O2 /std:c++17 /EHsc /MT vecbench.cpp /Fe:vecbench_cpp.exe
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <chrono>

int main(int argc, char **argv) {
    long long n = (argc >= 2) ? atoll(argv[1]) : 10000000;
    auto now = [] { return std::chrono::high_resolution_clock::now(); };
    auto us = [](auto a, auto b) {
        return (long long)std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
    };

    auto t0 = now();
    std::vector<long long> v;
    for (long long i = 0; i < n; i++) v.push_back(i % 1000);
    auto t1 = now();

    long long sum = 0;
    for (long long i = 0; i < n; i++) sum += v[i];
    auto t2 = now();

    long long sum2 = 0;
    for (long long x : v) sum2 += x;
    auto t3 = now();

    for (long long i = 0; i < n; i++) v[i] = v[i] + 1;
    auto t4 = now();

    long long chk = sum + sum2 + v[0] + v[n - 1];
    printf("result: %lld\n", chk);
    printf("[@bench] push      %lld us\n", us(t0, t1));
    printf("[@bench] index_r   %lld us\n", us(t1, t2));
    printf("[@bench] for_in    %lld us\n", us(t2, t3));
    printf("[@bench] index_w   %lld us\n", us(t3, t4));
    printf("[@bench] mean %lld us (%lld iterations)\n", us(t0, t4), n);
    return 0;
}
