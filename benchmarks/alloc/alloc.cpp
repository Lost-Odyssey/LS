// alloc.cpp — C++ (MSVC) reference for the LS alloc benchmark.
// std::vector / std::unordered_map / std::string — RAII baseline.
//   cl /O2 /std:c++17 /EHsc /MT alloc.cpp /Fe:alloc_cpp.exe && alloc_cpp.exe [n]
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

static long long vec_stress(long long n) {
    std::vector<std::string> v;
    for (long long i = 0; i < n; i++) {
        v.push_back("item_" + std::to_string(i));
    }
    long long chk = 0;
    for (auto &s : v) chk += (long long)s.size();
    return chk;
}

static long long map_stress(long long n) {
    std::unordered_map<std::string, long long> freq;
    long long keyspace = 8192;
    for (long long i = 0; i < n; i++) {
        std::string key = "key_" + std::to_string(i % keyspace);
        long long cur = 0;
        auto it = freq.find(key);
        if (it != freq.end()) cur = it->second;
        freq[key] = cur + 1;
    }
    return (long long)freq.size();
}

int main(int argc, char **argv) {
    long long n = (argc >= 2) ? atoll(argv[1]) : 200000;
    int iters = 5;

    volatile long long warm = vec_stress(n) + map_stress(n);
    (void)warm;

    long long total_ns = 0;
    long long chk = 0;
    for (int it = 0; it < iters; it++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        chk += vec_stress(n) + map_stress(n);
        auto t1 = std::chrono::high_resolution_clock::now();
        total_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    }
    double mean_ns = (double)total_ns / iters;
    printf("result: %lld\n", chk);
    printf("[@bench] mean %.1f ns (%d iterations)\n", mean_ns, iters);
    return 0;
}
