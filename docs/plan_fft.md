# FFT / DCT / DST 模块 — 设计与实现

> **日期**：2026-05-30（初稿）
> **基础库**：PocketFFT（BSD 3-Clause）
> **集成方式**：第三方源码静态链接进 `ls.exe`，纯 LS 标准库模块暴露 API
> **当前进度**：❌ 未开始

---

## 0. 背景

LS 目前有内建 `math` 模块（sqrt/sin/cos/log 等，LLVM intrinsic / libm），但缺乏：
- 复数 FFT（信号处理、SDR、5G 信道估计）
- DCT（图像/视频压缩、信道估计去噪）
- DST（PDE 求解器）

需要一个零外部 DLL 依赖的 FFT 方案。LS 已有 C FFI 可调任意 DLL，但**静态链接方案**更符合 LS 哲学（单一 `ls.exe`，仅依赖 LLVM）。

## 1. 库选型：PocketFFT

| 项目 | 值 |
|------|-----|
| 选型 | PocketFFT |
| 作者 | Martin Reinecke（MPCDF） |
| 许可证 | BSD 3-Clause |
| 语言 | C++（~3k 行） |
| 成熟度 | NumPy v1.17+ 使用，大规模验证 |

选择理由：
- **NumPy 在用**—替换了老 FFTPACK，精度和性能经过充分验证
- **任意长度**—大质因数用 Bluestein 算法，不退化到 O(N²)
- **原生 DCT/DST**—DCT-I~IV、DST-I~IV 全部支持，零额外工作
- **BSD 许可证**—静态链接无传染性，只需 NOTICE 文件

## 2. 四层架构

```
┌─────────────────────────────────────────────────┐
│  std/fft.ls                 纯 LS API 封装       │  ← 第 0 层（用户可见）
├─────────────────────────────────────────────────┤
│  runtime/ls_fft_wrapper.{h,cpp}  C 包装器        │  ← 第 1 层（C 胶水）
├─────────────────────────────────────────────────┤
│  lib/pocketfft/pocketfft.h     PocketFFT 源码     │  ← 第 2 层（第三方 C++）
├─────────────────────────────────────────────────┤
│  CMakeLists.txt + jit.c        构建集成          │  ← 第 3 层（静态链接 + JIT 符号注册）
└─────────────────────────────────────────────────┘
```

### 2.1 第 0 层：`std/fft.ls`（LS 模块）

用户入口。通过 `extern fn` 绑定第 1 层的 C 函数，再封装为 LS 惯用的高层 API。

### 2.2 第 1 层：`runtime/ls_fft_wrapper.{h,cpp}`（C 包装器）

PocketFFT 是 C++，LS 编译器和 JIT 需要 C ABI 符号。本层提供：

- `extern "C"` 函数声明（`runtime/ls_fft_wrapper.h`）
- C++ 实现（`runtime/ls_fft_wrapper.cpp`），调用 PocketFFT 的 C++ API
- 所有内存由调用方提供（避免跨语言内存管理问题）

### 2.3 第 2 层：`lib/pocketfft/`（第三方源码）

PocketFFT 的主要头文件 `pocketfft.h`（header-only 或单翻译单元），放入 `lib/` 目录。

### 2.4 第 3 层：构建集成

- `CMakeLists.txt`：将 `runtime/ls_fft_wrapper.cpp` 和 `lib/pocketfft/` 加入编译，**启用 C++ 编译器**（`enable_language(CXX)`），输出编入 `ls_os_backend.lib`（AOT）和 `ls.exe`（JIT 符号自动解析）
- `src/jit.c`：将 FFT 符号注册到 LLJIT 的 `AbsoluteSymbols`（如有必要）
- `NOTICE.md`：附 BSD 3-Clause 全文

## 3. C API 设计（第 1 层）

所有函数操作**调用方提供的缓冲区**，不自行 malloc。

```c
/* ---------- 复数 FFT 1D ---------- */
/* data: 交错排列 [re0, im0, re1, im1, ...] 共 2*n 个 double */
/* n: FFT 点数，可为任意正整数（非 2 的幂使用 Bluestein） */
/* direction: 0 = 正向 FFT, 1 = 逆 IFFT */
void ls_fft_c2c_1d(double *data, int n, int direction);

/* ---------- 实数 FFT 1D ---------- */
/* 正向: n 个实数输入 → n/2+1 个复数输出写入同一 buffer */
void ls_fft_r2c_1d(double *data, int n);
/* 逆向: n/2+1 个复数 → n 个实数 */
void ls_fft_c2r_1d(double *data, int n);

/* ---------- DCT 1D ---------- */
/* data: n 个 double, in-place */
/* type: 1~4 对应 DCT-I ~ DCT-IV */
void ls_fft_dct_1d(double *data, int n, int type);

/* ---------- DST 1D ---------- */
/* data: n 个 double, in-place */
/* type: 1~4 对应 DST-I ~ DST-IV */
void ls_fft_dst_1d(double *data, int n, int type);

/* ---------- 2D DCT（图像用）---------- */
/* data: w*h 个 double, row-major */
void ls_fft_dct_2d(double *data, int w, int h, int type);
```

### 3.1 复数 FFT 的实虚分离约定

5G 信道估计场景的输入是 N 点复数（频域信道估计值），DCT 作为线性正交变换在实部和虚部上**分别**执行。LS 层包装为两种 API：

```ls
// 方式 A：交错数组（更接近 C 布局）
fft.forward(data_re, data_im)  // 两路 vec(f64)，内部交错为 [r0,i0,r1,i1,...] 再调用 C

// 方式 B：复数 struct 数组
struct Complex { f64 real; f64 imag }
fft.forward(vec(Complex) data) // 编组为交错数组后再调用 C

// 方式 C：5G 风格，一个 vec(f64) 实部 + 一个 vec(f64) 虚部分别 DCT
fft.dct(real, imag, fft.DCT_II)  // 实部虚部分别做 DCT
```

建议三种都支持，内部统一收敛到方式 A 再调用 C。

## 4. LS API 设计（第 0 层）

```ls
import fft

// ---- FFT ----
fn forward(vec(f64) real, vec(f64) imag)                     // C2C 正向
fn inverse(vec(f64) real, vec(f64) imag)                     // C2C 逆向

// ---- DCT（四种类型） ----
fn dct(vec(f64) data, int type)                              // 1D in-place
fn dct(vec(f64) real, vec(f64) imag, int type)               // 复数 DCT（实虚分别）
fn dct_2d(vec(f64) data, int w, int h, int type)             // 2D in-place

// ---- DST（四种类型） ----
fn dst(vec(f64) data, int type)                              // 1D in-place
fn dst(vec(f64) real, vec(f64) imag, int type)               // 复数 DST

// ---- 常量 ----
const DCT_I  = 1
const DCT_II = 2     // JPEG / 默认
const DCT_III = 3
const DCT_IV = 4     // MDCT (AAC)
```

### 4.1 DCT 四种类型的物理意义

| 类型 | 边界条件 | 典型用途 |
|------|----------|----------|
| DCT-I | 两端偶对称（折点在整数端） | 边界精确重建 |
| **DCT-II** | 两端偶对称（折点半格点） | **JPEG / MP3 / H.264 / 5G 信道估计** |
| DCT-III | DCT-II 的逆变换 | 重建信号 |
| DCT-IV | 一端反对称、一端对称 | AAC MDCT |

5G NR SRS 信道估计使用 DCT-II 做变换域降噪：频域信道估计 → DCT-II → 延迟域剔除噪声基底 → 逆变换 → 平滑后的频域响应。

## 5. 实现步骤（按顺序）

### Step 1 — 基础构建集成

- 将 PocketFFT 源码放入 `lib/pocketfft/`（从 GitLab 拉取 `pocketfft.h`）
- 在根 `CMakeLists.txt` 中启用 C++（`enable_language(CXX)`）
- 创建 `runtime/ls_fft_wrapper.h` 和 `runtime/ls_fft_wrapper.cpp`，实现 `ls_fft_c2c_1d`
- 将 wrapper 加入 `ls_os_backend` 静态库，将 PocketFFT 加入链接
- 验证 `ls.exe` 能构建成功（JIT 模式不需要额外注册，符号已在进程空间）

### Step 2 — 实现 `std/fft.ls`（基础 FFT）

- `extern fn ls_fft_c2c_1d(...) -> void`
- 封装 `forward(real, imag)` / `inverse(real, imag)`：输入两路 `vec(f64)` → 交错为 `array(f64, 2*n)` → 调用 C → 拆回两路
- 编写测试 `tests/samples/fft_basic_test.ls`：验证 `forward(inverse(x)) ≈ x`

### Step 3 — 实现 DCT/DST

- 在 wrapper 中实现 `ls_fft_dct_1d`、`ls_fft_dst_1d`、`ls_fft_dct_2d`
- 在 `fft.ls` 中暴露 `dct(data, type)`、`dst(data, type)`、复数版本
- DCT 类型常量 `DCT_I` ~ `DCT_IV`
- 编写测试：
  - `dct_test.ls`：`dct(i_dct(x)) ≈ x`（DCT-II / DCT-III 互为逆）
  - `dct_2d_test.ls`：8×8 块 DCT，验证 JPEG 风格系数

### Step 4 — 复数 DCT（实虚分离）

- 实现 `ls_fft_dct_c2c_1d(data_re, data_im)`：分别调用 `ls_fft_dct_1d` 再合并
- 也可直接在 LS 层组合：`dct(real, imag, type)` → 分别调 `dct(real, type)` 和 `dct(imag, type)`

### Step 5 — memcheck + 边界覆盖

- 空 vec / 长度 0 输入 → 空操作
- 长度 1 → trivial 直通
- 非 2 幂长度 → Bluestein 自动降级
- 大长度（32768+）→ 无崩溃 / 无 OOM
- `ls run --memcheck` 验证 0 leaks / 0 dfree

## 6. 测试策略

| 类型 | 内容 | 验证方式 |
|------|------|----------|
| 单元测试 | C wrapper 内单个函数正确性 | CTest（`test_fft.c`） |
| E2E .ls | `fft.forward(inverse(x)) ≈ x` | `ls run fft_basic_test.ls` + PRNG 输入 |
| 精度测试 | 与 NumPy `np.fft.fft` / `scipy.fft.dct` 同输入对比 | Python 脚本生成测试向量，LS 加载比对 |
| 5G 场景 | 840 点复数 DCT-II → 阈值去噪 → 逆 DCT → 校验平滑度 | 定制 .ls 测试 |
| memcheck | 所有测试加 `--memcheck` | 0 leaks / 0 dfree |
| AOT | 编译为独立 .exe 运行 | `ls compile ... && .exe` 结果一致 |

## 7. 待讨论 / 后续

- **实数 FFT**（r2c / c2r）是否需要？当前阶段优先级低，可以先用 c2c 做一半长度的实数 FFT（虚部补零）
- **多维 FFT**（2D C2C、3D）？PocketFFT 原生支持，但 LS 包装工作量较大，延后
- **FFT 窗口函数**（hann、hamming、blackman 等）？属于信号处理预处理，可以另开 `fft.window` 模块或独立 `signal.ls`
- **复数类型** `struct Complex` 何时加入标准库？目前 `fft.ls` 内定义，后续可提取为 `std.complex`

## 8. 许可证注意事项

PocketFFT BSD 3-Clause 要求：
1. 源码分发需保留原始版权声明 + 许可证全文
2. 二进制分发需在文档/附带材料中复现

做法：项目根目录 `NOTICE.md` 包含 BSD 三条款全文，标注 `lib/pocketfft/` 中的代码为 Martin Reinecke 的 PocketFFT，BSD 3-Clause 许可。
