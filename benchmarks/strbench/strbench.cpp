// strbench.cpp — C++ std::string reference.
//   cl /O2 /std:c++17 /EHsc /MT strbench.cpp /Fe:strbench_cpp.exe
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

static std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == sep) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}
static std::string replace_all(const std::string &s, const std::string &from, const std::string &to) {
    std::string out;
    out.reserve(s.size());
    size_t p = 0;
    while (true) {
        size_t q = s.find(from, p);
        if (q == std::string::npos) { out.append(s, p, s.size() - p); break; }
        out.append(s, p, q - p);
        out.append(to);
        p = q + from.size();
    }
    return out;
}

int main(int argc, char **argv) {
    long long n = (argc >= 2) ? atoll(argv[1]) : 1000000;
    std::string base = "The Quick Brown Fox Jumps Over The Lazy Dog";
    std::vector<std::string> needles { "Fox", "Dog", "The", "Lazy", "Quick" };
    auto now = [] { return std::chrono::high_resolution_clock::now(); };
    auto us = [](auto a, auto b) {
        return (long long)std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
    };

    auto t0 = now();
    long long a1 = 0;
    for (long long i = 0; i < n; i++) {
        std::string u = base;
        std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return std::toupper(c); });
        a1 += u.size();
    }
    auto t1 = now();

    long long a2 = 0;
    for (long long i = 0; i < n; i++) {
        const std::string &nd = needles[i % 5];
        if (base.find(nd) != std::string::npos) a2++;
    }
    auto t2 = now();

    long long a3 = 0;
    for (long long i = 0; i < n; i++) { auto parts = split(base, ' '); a3 += (long long)parts.size(); }
    auto t3 = now();

    long long a4 = 0;
    for (long long i = 0; i < n; i++) { auto r = replace_all(base, "o", "0"); a4 += (long long)r.size(); }
    auto t4 = now();

    long long a5 = 0;
    for (long long i = 0; i < n; i++) { auto s = base.substr(i % 10, 5); a5 += (long long)(unsigned char)s[0]; }
    auto t5 = now();

    long long chk = a1 + a2 + a3 + a4 + a5;
    printf("result: %lld\n", chk);
    printf("[@bench] upper     %lld us\n", us(t0, t1));
    printf("[@bench] contains  %lld us\n", us(t1, t2));
    printf("[@bench] split     %lld us\n", us(t2, t3));
    printf("[@bench] replace   %lld us\n", us(t3, t4));
    printf("[@bench] substr    %lld us\n", us(t4, t5));
    printf("[@bench] mean %lld us (%lld iterations)\n", us(t0, t5), n);
    return 0;
}
