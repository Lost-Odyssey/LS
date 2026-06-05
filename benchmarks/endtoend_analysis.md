# LS 端到端 Benchmark 分析

## 基准测试概览

| 测试项 | 目录 | 内容 | 状态 |
|--------|------|------|------|
| Fibonacci | `benchmarks/fib/` | 递归斐波那契 fib(35) × 10 iters | ✅ |
| String Iteration | `benchmarks/string/` | 遍历 200K 字符串逐字节统计空格 × 10 iters | ✅ |

每个测试项均跨 5 种语言：
- **LS (JIT)** — `ls run`
- **LS (AOT)** — `ls compile → .exe`
- **C (MSVC)** — `cl /O2 /MT`
- **Python** — CPython
- **Ruby** — CRuby

---

## string benchmark 详细分析

### 测试内容

```ls
fn count_char(string s, char c, int end_) -> int {
    int count = 0
    for i in 0..end_ {
        if s.at(i) == c { count = count + 1 }
    }
    return count
}
```

构造 200,004 字符的模式串 `"a b c d e f g "` × 14286 次重复，遍历统计空格字符 `' '` 的出现次数。10 次迭代取平均耗时。

---

### 结果总表

| 语言 | mean (ns) | vs C | vs LS AOT | 正确性 |
|------|-----------|------|-----------|--------|
| **LS (AOT)** | **26,510** | **3.93× 快** | **1.00×** | ✅ `result=1000020` |
| C (MSVC) | 104,220 | 1.00× | 3.93× 慢 | ✅ |
| LS (JIT) | 347,200 | 0.30× | 13.09× 慢 | ✅ |
| Python | 1,158,480 | 0.09× | 43.70× 慢 | ✅ |
| Ruby | 2,730,440 | 0.04× | 103.0× 慢 | ✅ |

> Python/Ruby 仅跑 1 次迭代，表中 value 已 ×10 估算。

---

### 核心发现：LS AOT vs C MSVC

LS AOT 比 C MSVC 快 3.93×。反汇编揭示根本原因：

#### MSVC `/O2` — 标量循环，每次 1 字节

```asm
$LL9@count_char:
  cmp    BYTE PTR [r8+rcx], dl    ; 单字节比较
  lea    eax, DWORD PTR [r9+1]
  cmovne eax, r9d                 ; 条件递增
  inc    r8                        ; i++
  cmp    r8, r10
  jl     SHORT $LL9@count_char
```

MSVC 生成纯标量代码，每次迭代处理 1 字节。`/O2` 做了寄存器分配、指令选择等工作，但**不做循环向量化**。

#### LLVM AOT — SSE2 向量化，每次 16 字节

```asm
loop:
  movd    -0xc(%rsi,%rax), %xmm3     ; 加载 4 字节 (×4)
  movd    -0x8(%rsi,%rax), %xmm2
  movd    -0x4(%rsi,%rax), %xmm1
  movd    (%rsi,%rax), %xmm0         ; → 共 16 字节 / 迭代

  pcmpeqb %xmm6, %xmm3              ; 16 字节并行比较 space(0x20)
  punpcklbw %xmm3, %xmm3            ; 字节→字 展开
  punpcklwd %xmm3, %xmm3            ; 字→双字 展开
  pand    %xmm7, %xmm3              ; 掩码保留 LSB
  paddd   %xmm1, %xmm3              ; 累加至部分和

  ; ... 同上处理 xmm2, xmm1, xmm0 ...

  addq    $0x10, %rax                ; 前进 16 字节
  cmpq    $0x30d4c, %rax
  jne     loop
```

LLVM 将 `count_char` 的循环自动向量化为 **SSE2 SIMD**：
- `pcmpeqb` — 一次 16 字节并行比较（生成 0xFF/0x00 掩码）
- `punpcklbw` / `punpcklwd` — 水平展开以对齐求和
- `pand` + `paddd` — 累加比对结果
- 4 路 `movd` + 累加形成 16 字节/迭代的软件流水

尾部剩余 4 字节退化为标量 `cmpb $0x20` 处理。

#### 为什么 C 不用 SIMD？

MSVC 的 `/O2`（对应 `/Og /Oi /Ot /Oy /Ob2 /GF /Gy`）**不启用自动向量化**。需要额外加 `/arch:AVX2` + `/Qvec`（或 `/O2 /Qpar`）才能触发 SIMD。这是 MSVC 与 LLVM/Clang 自动向量化的已知差距：

| 编译器 | 优化选项 | 循环向量化 |
|--------|---------|-----------|
| MSVC 19.38 | `/O2` | ❌ 默认不启用 |
| LLVM 18 | `CodeGenLevelDefault` (O2) | ✅ 默认启用 |
| GCC 12+ | `-O2` | ✅ 默认启用 |

---

### 核心发现：LS JIT vs LS AOT

LS JIT (347,200 ns) 比 LS AOT (26,510 ns) 慢 13×。原因：

#### JIT 编译管道

`jit.c:build_jit_module()` 生成 LLVM IR → `jit_add_module()` → `LLVMOrcLLJITAddLLVMIRModule()`。

LLJIT (LLVM 18) 默认的编译管道**不含循环向量化 pass**，因为：
1. JIT 优先保证**编译速度**（低延迟启动）
2. 向量化显著增加编译时间（循环分析、成本建模、IR 变换）
3. 对于热循环，LLJIT 的默认 transform layer 使用保守优化

#### AOT 编译管道

`codegen.c` → `LLVMCodeGenLevelDefault` (O2) → `LLVMTargetMachineEmit`。

AOT 路径走完整 LLVM O2 管道，包含：
- `-loop-vectorize`
- `-slp-vectorize`
- `-licm`, `-gvn`, `-inline`, `-simplifycfg` 等

因此 AOT 获得了向量化收益，JIT 没有。

---

### 与 fib benchmark 对比

| benchmark | 操作 | C MSVC | LS AOT | LS JIT | JIT/AOT 比 |
|-----------|------|--------|--------|--------|-----------|
| fib | 递归调用 + 整数运算 | 36.9 ms | 27.2 ms | 46.8 ms | **1.72×** |
| string | 遍历 + 字符比较 | 0.104 ms | 0.0265 ms | 0.347 ms | **13.1×** |

- **fib** 递归不可向量化，差距来自 LLVM vs MSVC 的指令选择/内联优化，JIT/AOT 比仅 1.72×。
- **string** 可向量化，AOT 受益巨大（SSE2），JIT 不向量化甚至比 C 标量还慢。

---

### 对 LS 项目的影响

1. **JIT 性能上限**: 数值密集型/向量友好的代码在 AOT 下远优于 JIT。LS 可以作为"JIT 快速迭代 + AOT 最终部署"的双模式语言。
2. **LLJIT 调优空间**: 可通过 `LLVMOrcLLJITBuilderSetOptimizationLevel(LLVMCodeGenLevelAggressive)` 提升 JIT 优化级别，代价是每次编译延迟增加。
3. **JIT 预热策略**: 对关键循环可考虑 `ldc1` / `ldc2`（LLVM JIT layer 的 tiered compilation）渐进优化。
4. **MSVC 向量化**: C 对标里如果给 MSVC 加 `/arch:AVX2 /Qvec`，结果会更接近 LS AOT，但仍取决于 MSVC 的向量化能力。
