# structbench — compiler 内建 struct/enum 基准分析

## 目的

alloc / json 测的是运行时（分配、parser），fib / string 测的是循环 codegen。
structbench 专测 **LS 编译器对核心值类型的 codegen 质量**：struct 构造、字段访问、
方法分发、嵌套 struct、enum + match。纯计算、零堆分配 → 直接对标 C++/Rust 的内联+SROA。

三个维度（每个独立计时）：
- **scalar struct**：`Point{x,y}` 构造 + `dist2()` 方法调用
- **nested struct**：`Circle{center: Point, r}` 嵌套字段穿透 + `area()`
- **enum + match**：3-variant enum 构造 + `match` 解构分发

四语言：LS(JIT / JIT -O / AOT) / Rust(rustc -O) / C++(MSVC /O2) / Python。
坐标基用 `i % 1000` 让 checksum 在 1 亿次迭代下仍 i64/f64-exact（三语言一致）。

运行：`./run.ps1 -N 100000000`

## 结果（n=1 亿，三维度总耗时，越小越快）

| 名次 | 语言 | 总耗时 | vs Rust |
|------|------|--------|---------|
| 🥇 | **Rust (rustc -O)** | 406 ms | 1.00× |
| 🥈 | **LS (AOT)** | **510 ms** | 1.25× |
| 🥉 | **LS (JIT -O)** | **546 ms** | 1.34× |
| 4 | C++ (MSVC /O2) | 834 ms | 2.05× |
| 5 | LS (JIT 默认) | 2,114 ms | 5.20× |
| 6 | Python | ~131,655 ms | 324× |

> ⚠️ 计时报**总时间**，不报 per-iter ns —— 优化后 per-loop 仅几纳秒，`总/n` 的整除会把
> 1ns 和 2ns 都显示成 1，精度全失。总时间是诚实的度量。

### 关键发现

1. **LS AOT(510ms）反超 C++(834ms）**，仅比 Rust 慢 1.25×。struct/enum 的 codegen
   质量第一梯队 —— LLVM 完全内联 dist()/area() + SROA 把 struct 拆成寄存器。
2. **C++ 比 Rust 慢 2×** —— MSVC `/O2` 对小聚合的内联不如 Rust 激进（与 string benchmark
   里 MSVC 不自动向量化同源：MSVC 优化保守）。
3. **LS JIT 默认 5.2× → -O 1.34×** —— O2 内联的价值（见下）。
4. **碾压 Python 324×**。

## 修复的 bug（structbench 暴露）

- **bug #24**：循环内 struct 字面量 `sl.tmp` alloca 在循环体内 → JIT 1MB 栈溢出
  （n>50000）。AOT 不崩（O2 mem2reg）。修复：alloca 移到 entry block。详见 `bugs/24.txt`。
- **bug #25**：enum 布局 `{i8, [N x i8]}` byte 数组 → align 1 → f64/i64 payload 全是
  misaligned 访问。改对齐载体 `{i8, [K x i64]}` align 8 后 **enum 22→6ns（默认）、
  9→1ns（-O，追平 Rust）**。详见 `bugs/25.txt`。

## JIT `-O` 优化开关

`ls run -O`（= `--optimize`）在 IR 进 LLJIT 前跑完整 `default<O2>` pipeline（内联、循环
向量化、mem2reg、GVN、DCE），与 `clang -O2` / `rustc -O` 同一批 pass。默认关（LLJIT 裸
codegen 优先快速启动）。

### `-O` 的运行加速（structbench 各维度，n=1 亿）

| 维度 | JIT 默认 | JIT -O | 加速 |
|------|---------|--------|------|
| scalar | 962 ms | 107 ms | **9.0×** |
| nested | 452 ms | 206 ms | 2.2× |
| enum   | 695 ms | 234 ms | 3.0× |

其他 benchmark：string 18×、fib 1.7×；alloc/json 几乎无效（瓶颈在 runtime malloc /
函数调用密度，非 IR 层）。

### `-O` 的启动开销代价（各取 5 次最小值）

| 程序 | 默认启动 | -O 启动 | **-O 多花** |
|------|---------|---------|-----------|
| hello（1 行，最好情况） | 29 ms | 30 ms | **+1 ms** |
| struct+enum（小程序） | 31 ms | 35 ms | **+4 ms** |
| json import（大 std 模块，最坏情况） | 596 ms | 689 ms | **+93 ms** |

- 增量随**编译的 IR 规模**增长：小程序 ~1ms（淹没在噪声里），大模块（700 行 std.json）+93ms。
- 注意 ~30ms 基线是 LLVM init + LLJIT 建立 + 进程启动的固定开销，与 O2 无关；json 的
  596ms 基线主要是**编译大模块本身**（默认 codegen 也要编译，只是不优化）。

### 何时用 `-O`

| 场景 | 选择 |
|------|------|
| 快速脚本 / REPL / 小程序 | 默认（省几 ms；O2 收益也小） |
| 计算密集 / 热循环 / 跑很久 | **`-O`**（+1~93ms 启动换 2-9× 加速，稳赚） |
| 一次性大数据处理 / 最终部署 | **`-O`** 或 `compile` 成 AOT |

**经验法则**：程序运行超过 ~0.5 秒，`-O` 的启动开销就被运行加速赚回来了。
（structbench 1 亿次：默认 2.1s → -O 0.55s，+93ms 启动换 1.5s 计算。）
