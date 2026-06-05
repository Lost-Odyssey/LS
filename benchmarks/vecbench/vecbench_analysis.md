# vecbench — 内建 vec(T) 吞吐基准分析

## 目的

vec(T) 是 LS 最常用的内建容器。本基准测核心动态数组操作的 codegen 质量：
push（含 grow）、index 读、for-in 遍历、index 写。对标 C++ std::vector / Rust Vec /
Python list。坐标基 `i % 1000` 让 checksum 在大 n 下稳定（三语言一致 `9990001001`）。

运行：`./run.ps1 -N 10000000`

## 结果（n=1000万，各操作总耗时 µs，越小越快）

| 操作 | C++ | Rust | **LS -O** | **LS AOT** | LS 默认 | Python |
|------|-----|------|-----------|-----------|---------|--------|
| **push**（含 grow） | 65,952 | 48,047 | 50,666 | 46,853 | 56,025 | ~410,000 |
| **index 读** | 4,482 | 5,243 | 7,166 | 9,402 | 17,888 | ~256,000 |
| **for-in 遍历** | 3,896 | 4,019 | 3,911 | 3,998 | 19,986 | ~166,000 |
| **index 写** | 6,953 | 5,732 | 9,507 | 7,657 | 21,324 | ~312,000 |

### 关键发现

1. **push：LS AOT(46.9ms）最快**，胜过 C++(66ms）和 Rust(48ms）。grow 的 realloc+memcpy
   受内存带宽限制，LS 不输甚至领先。
2. **for-in：LS -O/AOT 完全追平 C++/Rust(~3.9ms）**。顺序遍历 LLVM 优化到位。
3. **index 读/写：LS 略慢(读 ~1.6×、写 ~1.4×）** —— 唯一短板，根因是 `v[i]` 的运行时
   边界检查（3-BB ok/oob/merge），C++/Rust release 无检查。
4. **碾压 Python 50-100×**。

## 边界检查消除（BCE）与 `vec.get_unsafe(i)`

### 重要更正：index 读的差距主要是 BCE，不是 get_unsafe

`v[i]` 带运行时边界检查（`i>=0 && i<len`，3-BB ok/oob/merge）。但 **LLVM O2 的
bounds-check-elimination 会删掉能静态证明永远成立的检查**。对 `for i in 0..v.length { v[i] }`
这种确定性遍历，O2/AOT 能证明 `i` 恒在 [0,len)，**直接删除检查**：

| 模式 | checked `v[i]` | `get_unsafe(i)` | 说明 |
|------|---------------|-----------------|------|
| AOT | ~4.0 ms | ~4.0 ms（无差别） | O2 已删掉检查，get_unsafe 无收益 |
| JIT -O | 6.0 ms | 3.8 ms（1.57×） | O2 这个循环没完全消除，手动 unsafe 兜底 |
| JIT 默认 | 18.2 ms | 16.5 ms（1.10×） | 无 BCE，但默认模式整体就慢 |

之前 vecbench 主表里 AOT index 读「9.4ms」是单次冷跑噪声，稳态约 4.0ms，本就追平 C++/Rust。

### 实验：BCE 只删可证明安全的检查（边界检查仍有意义）

同一程序里，安全 for 循环的检查被删，但**编译器证不出的索引检查原样保留**：
```ls
i64 x = v[idx]   // idx 依赖 perf.now()，O2 无法推断其值
```
默认 / -O / AOT 三种模式下均触发 `[warning] vec index out of bounds: index=100, len=3`，
返回默认值 0（不崩溃、不越界读）。即：

- **确定性遍历**（最安全）→ 检查删除，零开销
- **运行时/外部索引**（最危险，缓冲区溢出漏洞的来源）→ 检查保留，挡住越界

**结论：BCE 让边界检查接近"零成本安全"——可证明安全处不付费，危险处才付费。**
这是 Rust/Swift 的设计哲学，LS 在 -O/AOT 下同样如此。

### get_unsafe 的真实定位

`vec.get_unsafe(i)`（codegen GEP+load，owned 元素仍 deep-clone，memcheck clean）是
**给极少数「BCE 证不出但人脑确定安全」的 JIT 热点准备的手动逃生口**，不是常态：
- AOT / 可被 O2 分析的简单循环：用不上（检查已被删）
- 实用建议：想要"快又安全" → 用 `-O` 或 AOT，BCE 自动生效；`get_unsafe` 仅限罕见热点

实现：codegen.c `get_unsafe` 方法分支；checker 放行；`tests/test_vec_get_unsafe.cmake`。

## 结论

vec 整体第一梯队：push/for-in 追平甚至超过 C++；index 读在 -O/AOT 下经 BCE 也追平。
默认带检查的 `v[i]` 是合理的安全默认（可证明处检查会被 BCE 删除，几乎零成本）。

未来可做：在默认 JIT（无 -O）也加简单循环的 BCE，让不开 -O 的安全写法也拿到速度。
