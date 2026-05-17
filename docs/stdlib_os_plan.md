# LS stdlib OS 抽象层优化 — 项目说明书

**版本**: 1.0  **日期**: 2026-05-15  **状态**: Phase 1 实施中

---

## 一、背景与动机

当前 `stdlib/proc.ls` 和 `stdlib/env.ls` 存在大量 `#if WINDOWS / #else / #end` 条件编译块。这是早期快速迭代的结果，但随着 stdlib 规模扩大，带来以下问题：

| 问题 | 影响 |
|------|------|
| 平台逻辑散落在 LS 源文件中 | 新增平台需要改动每个 stdlib 文件 |
| LS 层暴露 `_popen` / `_putenv_s` 等 C 内部符号 | API 不稳定，跨平台语义不一致 |
| `runtime/builtins.c` 内部也有 `#ifdef _WIN32` | 单个 C 文件承担多平台责任 |
| 测试矩阵不完整 | 不同平台分支难以独立测试 |

**目标**：将所有平台差异收敛到两个 C 文件（`runtime/os_win32.c` / `runtime/os_posix.c`），LS stdlib 和 `builtins.c` 完全无 `#if` 条件。

---

## 二、架构总览

```
┌─────────────────────────────────────────────────────────────┐
│  Level 4: LS 用户代码                                        │
│    import proc   import env   import io                      │
├─────────────────────────────────────────────────────────────┤
│  Level 3: stdlib LS 文件（纯 LS，无 #if）                   │
│    stdlib/proc.ls   stdlib/env.ls   stdlib/os.ls (Phase 2)  │
│    只调用 extern fn ls_os_*（稳定 C API）                   │
├─────────────────────────────────────────────────────────────┤
│  Level 2: OS 后端 C 文件（唯一有 #ifdef 的层）              │
│    runtime/os_win32.c  ←── CMake if(WIN32)                  │
│    runtime/os_posix.c  ←── CMake else                       │
│    导出统一的 ls_os_* 符号集                                │
├─────────────────────────────────────────────────────────────┤
│  Level 1: runtime/builtins.c（语言运行时，无 OS 系统调用）  │
│    malloc/free wrapper, print helpers, argc/argv,           │
│    __ls_proc_* 和 __ls_env_* thin wrapper → 调用 ls_os_*   │
├─────────────────────────────────────────────────────────────┤
│  Level 0: OS 内核 / libc                                    │
└─────────────────────────────────────────────────────────────┘
```

**三问健康检查**（每次改动前自检）：

> 1. 这段代码是否包含平台差异？→ 若是，放入 `os_win32.c` 或 `os_posix.c`
> 2. 这段代码是否是语言运行时基础设施？→ 若是，放入 `builtins.c`
> 3. 这段代码是否是 LS 语义 API？→ 若是，用纯 LS 写在 stdlib

---

## 三、各层职责详表

### Level 2 — OS 后端导出的 `ls_os_*` API（Phase 1）

```c
/* 进程执行（exec_full 后端） */
void        ls_os_exec_run(const char *cmd);
void       *ls_os_exec_take_stdout(void);
long long   ls_os_exec_stdout_len(void);
void       *ls_os_exec_take_stderr(void);
long long   ls_os_exec_stderr_len(void);
int         ls_os_exec_get_code(void);
int         ls_os_exec_get_ok(void);

/* 环境变量快照 */
void        ls_os_env_prepare(void);
int         ls_os_env_count(void);
const char *ls_os_env_entry(int i);
```

### Level 1 — builtins.c 保留的职责

```
- 内存分配 helper（malloc/free/realloc wrappers）
- print / print_int / print_f64 / print_bool
- argc/argv 全局存储（ls_main_argc / ls_main_argv）
- __ls_get_argc / __ls_get_argv / __ls_init_args（JIT 初始化）
- __ls_proc_exit（exit wrapper）
- __ls_proc_exec_full_* thin wrappers → 调用 ls_os_exec_*
- __ls_env_all_* thin wrappers → 调用 ls_os_env_*
```

---

## 四、防冻结机制（`raw_*` 前缀）

Phase 1 的 `ls_os_*` API 是 libc 的一对一镜像，刻意冠以 `raw_` 前缀的语义：
`ls_os_exec_run` 是实现层符号，不对外成为 LS 的公开 API。

Phase 2 引入 `stdlib/os.ls` 时，面向用户的 API 会经过语义包装：

```ls
// stdlib/os.ls (Phase 2 示例，尚未实现)
module os

extern fn ls_os_exec_run(string cmd)         // 调用后端
extern fn ls_os_exec_take_stdout() -> *u8
// ...

fn exec_raw(string cmd) -> ExecResult { ... }  // 用户友好 API
```

**规则**：`ls_os_*` 符号不得直接暴露给 LS 用户作为 stdlib 公开 API；必须经过 stdlib 的 LS 层包装。

---

## 五、Phase 1 实施计划

### Step #1：创建 OS 后端文件（本次实施）

| 任务 | 文件 |
|------|------|
| 创建 Windows 后端 | `runtime/os_win32.c` |
| 创建 POSIX 后端 | `runtime/os_posix.c` |
| 创建共享接口头文件 | `runtime/os_backend.h` |
| 更新 builtins.c（移除 #ifdef，改调 ls_os_*） | `runtime/builtins.c` |
| 更新 CMakeLists.txt（条件编译路由） | `CMakeLists.txt` |

**验收标准**：
- `builtins.c` 中零 `#ifdef _WIN32` / `#ifdef _WIN32` 等条件
- `cmake --build build` 成功
- `ctest --test-dir build -C Release` 31/31 通过

### Step #2：提取 popen 差异（下一步）

将 `stdlib/proc.ls` 中的 `#if WINDOWS` popen/pclose 分支迁移到 C 后端：

```c
/* 新增 OS 后端 API */
void *ls_os_popen(const char *cmd);     // Windows: _popen; POSIX: popen
int   ls_os_pclose(void *fp);           // Windows: _pclose; POSIX: pclose
int   ls_os_pid(void);                  // Windows: _getpid; POSIX: getpid
int   ls_os_wait_exit_code(int raw);    // Windows: raw; POSIX: WEXITSTATUS
```

然后 `stdlib/proc.ls` 改为无 `#if`。

### Step #3：提取 env.set/delete 差异（后续）

将 `stdlib/env.ls` 中的 `_putenv_s` / `setenv` 分支迁移到 C 后端：

```c
int ls_os_setenv(const char *name, const char *value);
int ls_os_unsetenv(const char *name);
```

---

## 六、Phase 2 计划（概要）

创建 `stdlib/os.ls`——纯 LS 语义层，作为其他 stdlib 模块调用 OS 功能的统一入口。

```ls
module os
// import proc 可以改为 import os

fn exec(string cmd) -> ExecResult { ... }  // 调用 ls_os_exec_*
fn pid() -> int { ... }
fn env_get(string name) -> Option(string) { ... }
// ...
```

**好处**：
- `proc.ls` / `env.ls` 内部调用 `os.*` 而非 libc 函数，彻底去除 `#if`
- 新增平台只需添加 `os_newplatform.c` + CMake 一行

---

## 七、文件结构（Phase 1 后）

```
runtime/
  os_backend.h     ← ls_os_* API 声明（平台无关接口）
  os_win32.c       ← Windows 实现（唯一含 WIN32 API 调用的文件）
  os_posix.c       ← POSIX 实现（唯一含 fork/pipe/environ 的文件）
  builtins.c       ← 语言运行时（无 #ifdef OS，只调 ls_os_*）
  memcheck.c       ← 内存检查器（不涉及 OS）

stdlib/
  proc.ls          ← Phase 2 后无 #if；Phase 1 暂保留
  env.ls           ← Phase 2 后无 #if；Phase 1 暂保留
  io.ls            ← 已无 OS 差异
  os.ls            ← Phase 2 新增
```

---

## 八、CMakeLists 路由策略

```cmake
# 唯一的平台判断点
if(WIN32)
    set(LS_OS_BACKEND runtime/os_win32.c)
else()
    set(LS_OS_BACKEND runtime/os_posix.c)
endif()

# 所有需要 builtins 的目标同时链接 OS 后端
add_executable(ls ${LS_SOURCES} ${LS_OS_BACKEND})
add_library(ls_builtins STATIC runtime/builtins.c ${LS_OS_BACKEND})
# 同样添加到 test_jit / test_ffi / test_module / test_memory
```

**原则**：`if(WIN32)` 在 CMakeLists 中只出现一次用于选择 OS 后端文件；其余所有 `if(WIN32)` 仅用于链接系统库（Shlwapi、Ws2_32 等）。

---

## 九、实施进度

| 步骤 | 状态 |
|------|------|
| Phase 1 Step #1：os 后端 C 文件 + builtins 重构 | 🟡 进行中 |
| Phase 1 Step #2：popen/pid/wait_exit 迁移 | ⬜ 待定 |
| Phase 1 Step #3：setenv/unsetenv 迁移 | ⬜ 待定 |
| Phase 2：stdlib/os.ls 语义层 | ⬜ 规划中 |
