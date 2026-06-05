// treebench.cpp — C++ reference: tree traversal by pointer (zero-copy).
//   cl /O2 /std:c++17 /EHsc /MT treebench.cpp /Fe:treebench_cpp.exe
#include <cstdio>
#include <cstdlib>
#include <chrono>

struct Tree { long long val; Tree* l; Tree* r; bool leaf; };

static Tree* build(int depth, long long v) {
    Tree* t = new Tree();
    if (depth == 0) { t->leaf = true; t->val = v; return t; }
    t->leaf = false;
    t->l = build(depth - 1, v * 2);
    t->r = build(depth - 1, v * 2 + 1);
    return t;
}
static long long sum_tree(const Tree* t) {
    if (t->leaf) return t->val;
    return sum_tree(t->l) + sum_tree(t->r);
}

int main(int argc, char** argv) {
    int depth = (argc >= 2) ? atoi(argv[1]) : 16;
    int iters = 20;
    Tree* tr = build(depth, 1);
    volatile long long s = 0;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iters; i++) s = sum_tree(tr);
    auto t1 = std::chrono::high_resolution_clock::now();
    double mean = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()
                  / iters / 1000.0;
    printf("result: %lld\n", (long long)s);
    printf("[@bench] mean %.1f us (depth=%d, %d sum)\n", mean, depth, iters);
    return 0;
}
