# alloc benchmark — 分配 / RAII / 容器吞吐分析

## 目的

fib / string 两个 benchmark 都是纯 CPU、零堆分配，测的其实是 LLVM 后端质量
（自动向量化），而非 LS 自己的实现。alloc benchmark 专门压 **LS 区别于 C 的那一层**：
RAII drop/clone、move 语义、vec/map/string 容器、f-string 格式化。

## 工作负载

每次迭代（`iters=5`，取每迭代均值）跑两个阶段：

- **Phase A `vec_stress`**：`vec(string)` push `n` 个堆字符串 `f"item_{i}"`，遍历求和长度，作用域退出全部 drop。
- **Phase B `map_stress`**：`map(string,int)` 词频统计，`keyspace=8192` 个不同 string key，`n` 次 token → 触发多次 rehash。

三类参照实现复刻**完全相同**的校验和（相同字符串格式 + 相同 map 语义）：
LS(JIT/AOT) / Rust(`rustc -O`) / C++(MSVC `/O2`) / Python / Ruby。

运行：`./run.ps1 -N 200000`（N 经 `proc.args()` 读入，bug #22 修复后 AOT 也可用）。

## 结果（n=200000，per-iter mean）

### 优化后（f-string 精确分配 + itoa 快路径，2026-06-04）

| 语言 | mean (ns) | vs C++ | 正确性 |
|------|-----------|--------|--------|
| **C++ (MSVC /O2)** | 25,081,920 | **1.00×** | ✅ 10485410 |
| **LS (AOT)** | **53,192,560** | **2.12×** | ✅ |
| LS (JIT) | 58,572,840 | 2.34× | ✅ |
| **Rust (rustc -O)** | 64,790,380 | 2.58× | ✅ |
| Python (CPython 3.9) | 128,348,580 | 5.12× | ✅ |
| Ruby (3.4) | 187,127,940 | 7.46× | ✅ |

**LS AOT/JIT 现已反超 Rust（2.12× / 2.34× vs 2.58×），距 C++ 仅 ~2.1×。**

中间态（仅精确分配，未加 itoa）：LS AOT 72.65M（2.94×）、JIT 80.08M（3.24×）。

### 优化前（fixed malloc(4096)）

| 语言 | mean (ns) | vs C++ |
|------|-----------|--------|
| C++ | 24,329,400 | 1.00× |
| Rust | 63,430,740 | 2.61× |
| Python | 125,060,660 | 5.14× |
| Ruby | 169,994,200 | 6.99× |
| **LS (AOT)** | 354,624,260 | **14.58×** |
| **LS (JIT)** | 377,063,360 | 15.50× |

> 优化效果：**LS AOT 4.85× 提速（14.58×→2.88×），LS JIT 3.8× 提速**。从「比 Python 慢
> 2.8×」翻转为「比 Python 快 1.8×」。

注意：alloc 不可向量化，**LS AOT ≈ JIT**，AOT 的向量化优势在此消失（对比 string benchmark）。

## 瓶颈定位

`--profile`（n=20000）：

```
  6   169.395 ms   80.7%  vec_stress
  6    40.599 ms   19.3%  map_stress
```

vec 阶段占 80%。进一步分解（`/tmp/probe.ls`，n=200000）：

| 操作 | 耗时 | 说明 |
|------|------|------|
| `push(f"item_{i}")` | **286,602 µs** | f-string |
| `push("item_x")` 静态 | 2,503 µs | 无 f-string |
| `for s in v` 求和 | 2,934 µs | for-in **不是**瓶颈 |
| 索引求和 | 2,827 µs | 与 for-in 持平 |

**结论：`f"item_{i}"` 比静态字符串慢 ~114×（≈1.43µs/次），是唯一瓶颈。**
for-in copy-out 几乎免费（与索引访问持平）。

### 根因（`src/codegen.c: codegen_format_string` ~L5112）

每个含插值的 f-string 发射的代码：

1. `malloc(4096)` — **固定 4096 字节堆缓冲**，无论结果是 7 字节还是多少；
2. `sprintf(buf, "item_%d", i)` — MSVC CRT 的 `sprintf`/`%d` 极慢（locale + printf 状态机，~1µs/次）；
3. `strlen(buf)` 量长度；
4. 返回 `LsString{buf, len, cap=4096}` → 析构时 `free(4096)`。

per-element 成本随 n **恒定**（n=20000 与 n=200000 均 ~1.43µs/个）→ 排除缓存/内存膨胀，
确认是**每次调用的固定 CRT + malloc 开销**主导。

## 可优化点（按价值排序）

1. ✅ **已完成（2026-06-04）：缓冲区按需分配 + 有界格式化**。`codegen_format_string` 改为：
   `n = __ls_fstr_format(tmp, 256, fmt, ...)`（runtime 包装 `vsnprintf`，**有界、不溢出**，
   返回完整所需长度）写入 entry-block 复用的 **256 字节**栈缓冲 → `malloc(n+1)` →
   运行时分支：`n<256` 走 `memcpy`（快路径），否则回退到第二次 `__ls_fstr_format` 直接
   格式化进精确堆缓冲。`cap=n+1`。
   - 根除每个 f-string 提交一个 4096 页 → 缺页中断。实测 f-string `1562ns → 240ns/个`
     （6.5×），整体 **LS AOT `354.6M → 72.7M ns`（4.88×）、LS JIT `377M → 80M`（4.7×）**。
   - 同时**消除栈/堆溢出 UB 并取消 4096 硬上限**（旧 `sprintf` 无边界；>256 自动回退）。
     验证 302/600 字节 f-string 正确，边界 255(快)/256(回退) 正确，memcheck clean。
   - Windows/UCRT 的 `snprintf` 是 header inline 无 JIT 可解析符号，故用 runtime 真实导出
     符号 `__ls_fstr_format`（builtins.c，AOT 链接 + jit.c 注册）。
2. ✅ **已完成（2026-06-04）：整数格式化绕开 MSVC vsnprintf**。`__ls_fstr_format`（runtime）
   加快路径：格式串只含 `%d/%i/%u/%lld/%llu/%s/%c/%%`（无浮点/宽度/精度/flag）时用自研
   itoa + memcpy 组装，绕开 CRT printf 状态机；其余（`%f`/`:.2f`/`%x`/`%p`/padding）回退
   `vsnprintf`，行为不变。`ls_fmt_is_simple` 与 `ls_fast_vformat` specifier 集合严格同步。
   实测 f-string `240ns → 117ns/个`（2.05×，**累计自原始 1562ns → 117ns = 13.3×**），
   **LS AOT 72.7M → 53.2M、JIT 80M → 58.6M，反超 Rust**。memcheck clean，json round-trip 正确。
3. **map 双重哈希**：`get(key)` 后 `set(key,...)` 对 key 哈希两次。可提供 `get_or_insert`
   / entry-API 合并为一次哈希 + 一次查找。
4. **string SSO（小字符串优化）**：Rust/C++ 对 ≤15~22 字节字符串内联存储零堆分配；
   LS 每个动态 string 都堆分配。SSO 可同时受益 vec/map/f-string 全链路。

> 第 1 项已落地（本 benchmark 最大收益点）。2~4 是后续系统性优化。

## 顺带发现的既有 bug（与本优化无关）

`i64` 字面量 / 插值被截断成 i32：`i64 big = 9000000000; print(big)` 输出 `410065408`
（= 9000000000 mod 2³²）。`print(big)` 不经 f-string 也复现 → 根因在 i64 字面量
codegen 或 print 路径，非 f-string。待单独修。
