# LS `proc` + `env` 模块实现说明书

## 0. 定位与边界

```
proc  = 进程层：外部命令执行 / 自身 args / pid / exit
env   = 环境变量：读写 / 枚举
───────────────────────────────────────────────────────
io    = 已有：文件 I/O；v3 加 read_line（stdin 归 io）
thread = 未来：pthread / channel（与 proc 无交集）
```

**`proc.exec*` 全部同步阻塞**，无后台/异步执行——留给未来 `thread` 模块。

---

## 1. 完整 API

### 1.1 `env` 模块

```ls
import env

// 读取
Option(string) v = env.get("PATH")          // 不存在 → None
string v         = env.get_or("PATH", "")   // 带默认值
string v         = env.require("HOME")      // 不存在 → 打印错误并 exit(1)

// 写入 / 删除
env.set("FOO", "bar")      // 设置
env.delete("FOO")           // 删除

// 查询
bool ok               = env.has("DEBUG")
map(string,string) all = env.all()          // 全部环境变量
```

### 1.2 `proc` 模块

```ls
import proc

// 自身信息
vec(string) args = proc.args()   // 不含 argv[0]（同 Ruby ARGV）
string name      = proc.program() // argv[0]，程序路径
int pid          = proc.pid()
proc.exit(0)

// 外部命令 — 三个级别
Result(string, string)     r = proc.exec("git log --oneline")
int                        n = proc.run("make build")
Result(ExecResult, string) r = proc.exec_full("cmake --build .")
```

### 1.3 `ExecResult` struct

```ls
// 编译器自动注册，proc 模块导出
struct ExecResult {
    string stdout
    string stderr
    int    exit_code
}
```

`ExecResult` 含 string 字段，`has_drop=true`，`__drop` 自动生成。

---

## 2. 函数语义

| 函数 | stdout | stderr | exit code | 返回值 |
|------|--------|--------|-----------|--------|
| `proc.exec(cmd)` | 捕获（含 stderr via `2>&1`） | 合并 | 0=Ok, 非0=Err | `Result(string,string)` |
| `proc.run(cmd)` | 透传终端 | 透传终端 | 直接返回 | `int` |
| `proc.exec_full(cmd)` | 独立捕获 | 独立捕获 | ExecResult.exit_code | `Result(ExecResult,string)` |

- `exec` 错误路径：Err 的 string = 捕获到的输出（已含 stderr）+ 退出码
- `run` 失败（进程无法启动）：返回 -1
- `exec_full` Err 路径：仅在进程无法启动时，正常退出码放进 ExecResult

---

## 3. 实现方式

**与 `math`/`io` 相同：编译器内建（compiler built-in）**，非纯 LS extern。

原因：
- `proc.exec_full` 需要跨平台 CreateProcess/fork+exec，逻辑量超出当前 extern FFI 能力
- `proc.args()` 需要访问编译器注入的全局 argc/argv
- 未来 extern struct + C ABI 完善后可迁移为纯 LS，接口不变

### 文件结构

```
src/
  builtins_proc.c      — 类型注册（ExecResult struct、函数签名）
  builtins_proc_cg.c   — codegen（emit LLVM IR）
  builtins_env.c       — 类型注册（env 函数签名）
  builtins_env_cg.c    — codegen（emit LLVM IR）

runtime/
  proc_helpers.c       — exec_full 的 C helper（平台差异在此隔离）
```

---

## 4. `proc.args()` — argc/argv 存储

### 4.1 全局变量（`src/main.c`）

```c
/* 由 main() 在最早期设置，供 proc.args() 使用 */
int   ls_main_argc = 0;
char **ls_main_argv = NULL;
```

`ls_run_file` / `ls_jit_main` 等入口处在调用任何 codegen 之前先赋值：

```c
ls_main_argc = argc;
ls_main_argv = argv;
```

### 4.2 JIT 符号注册（`src/jit.c`）

```c
// jit_init 里加入两个符号：
LLVMOrcAbsoluteSymbols symbols[] = {
    { "__ls_main_argc", (uint64_t)(uintptr_t)&ls_main_argc },
    { "__ls_main_argv", (uint64_t)(uintptr_t)&ls_main_argv },
    // ...existing symbols...
};
```

### 4.3 Codegen（`builtins_proc_cg.c`）

`proc.args()` 的 IR 逻辑（伪代码）：

```
1. load i32  @__ls_main_argc
2. load ptr  @__ls_main_argv
3. argc_skip = argc - 1           // 跳过 argv[0]
4. vec = __ls_vec_alloc(argc_skip)
5. for i in 1..argc:
     s = argv[i]  (char*)
     len = strlen(s)
     ls_str = make_string_from_ptr(s, len)  // 复制进 LsString
     vec_push(vec, ls_str)
6. return vec
```

`proc.program()` 直接读 `argv[0]` 转 LsString。

---

## 5. `env` 模块 codegen

所有函数直接 emit IR 调用 libc，零 C helper。

### 5.1 `env.get(name) → Option(string)`

```
1. call @getenv(name.data) → char*  [返回 NULL 或 指针]
2. icmp eq ptr, null
3. true  → 构造 None（disc=1）
4. false → strlen → make_string_copy → 构造 Some(s)（disc=0, payload=LsString）
5. return Option(string) alloca
```

平台：`getenv` 在 Windows/Linux 均为 libc 标准函数，无差异。

### 5.2 `env.get_or(name, default) → string`

```
1. call @getenv(name.data) → char*
2. if null → return default（clone，调用方持有 owned copy）
3. else   → strlen → make_string_copy → return
```

### 5.3 `env.require(name) → string`

```
1. call @getenv(name.data) → char*
2. if null:
     call @fprintf(stderr, "env: required variable '%s' not set\n", name.data)
     call @exit(1)
3. else → strlen → make_string_copy → return
```

### 5.4 `env.set(name, value)`

```c
// 平台差异：
#ifdef _WIN32
    _putenv_s(name, value)   // MSVC
#else
    setenv(name, value, 1)   // POSIX，1 = 覆盖
#endif
```

Codegen 用 `#if _WIN32` 选择调用符号：
- Windows：emit `call @_putenv_s(name.data, value.data)`
- Linux：emit `call @setenv(name.data, value.data, i32 1)`

### 5.5 `env.delete(name)`

```c
#ifdef _WIN32
    _putenv_s(name, "")      // Windows：设为空字符串 = 删除
#else
    unsetenv(name)           // POSIX
#endif
```

### 5.6 `env.has(name) → bool`

```
1. call @getenv(name.data)
2. icmp ne ptr, null → i1
3. zext i1 → i32 返回
```

### 5.7 `env.all() → map(string,string)`

```c
// 平台差异较大：
// Windows：GetEnvironmentStringsA() → "KEY=VAL\0KEY2=VAL2\0\0"
// Linux：  extern char **environ（POSIX，"KEY=VAL" 数组，NULL 结尾）
```

Codegen 方案：调用 C helper `__ls_env_all`（`runtime/proc_helpers.c`），返回填好的 `LsMap`。

```c
// runtime/proc_helpers.c
void __ls_env_all(LsMap *out_map) {
#ifdef _WIN32
    char *env_block = GetEnvironmentStringsA();
    char *p = env_block;
    while (*p) {
        char *eq = strchr(p, '=');
        if (eq && eq != p) { /* split key/val, map_set */ }
        p += strlen(p) + 1;
    }
    FreeEnvironmentStringsA(env_block);
#else
    extern char **environ;
    for (char **e = environ; *e; e++) {
        char *eq = strchr(*e, '=');
        if (eq) { /* split key/val, map_set */ }
    }
#endif
}
```

---

## 6. `proc.exec` / `proc.run` — popen 方案

### 6.1 `proc.run(cmd) → int`

最简单，直接 emit：

```
call @system(cmd.data) → i32 exit_code
```

- Windows：`system()` 调 `cmd.exe /c`，返回退出码
- Linux：`system()` 调 `/bin/sh -c`，需 `WEXITSTATUS(ret)`

```c
// Codegen 在 Linux 下额外 emit：
// result = (result >> 8) & 0xFF   （WEXITSTATUS 等价）
```

进程无法启动时 `system()` 返回 -1，直接透传。

### 6.2 `proc.exec(cmd) → Result(string,string)`

用 `popen`，将 stderr 重定向到 stdout（追加 ` 2>&1`）：

```c
// Codegen emit 的逻辑（runtime helper）：
char redirected[cmd_len + 8];
snprintf(redirected, sizeof(redirected), "%s 2>&1", cmd);

#ifdef _WIN32
FILE *fp = _popen(redirected, "r");
#else
FILE *fp = popen(redirected, "r");
#endif

if (!fp) → return Err("failed to start process")

// 读取输出（动态缓冲区）：
char   buf[4096];
char  *out = malloc(4096); int cap = 4096, len = 0;
while (fgets(buf, sizeof(buf), fp)) {
    int n = strlen(buf);
    if (len + n + 1 > cap) { cap *= 2; out = realloc(out, cap); }
    memcpy(out + len, buf, n);
    len += n;
}
out[len] = 0;

#ifdef _WIN32
int code = _pclose(fp);
#else
int raw  = pclose(fp);
int code = WEXITSTATUS(raw);
#endif

if (code == 0) → return Ok(LsString{out, len, cap})
else           → return Err(LsString{out, len, cap})
```

实现方式：C helper `__ls_proc_exec(char *cmd, LsResult *out)` in `runtime/proc_helpers.c`，codegen emit `call @__ls_proc_exec`。

### 6.3 `proc.exec_full(cmd) → Result(ExecResult,string)`

需要独立 stdout/stderr 管道，复杂度最高，完全封装在 C helper 中。

#### Linux 实现

```c
// runtime/proc_helpers.c — Linux 路径
void __ls_proc_exec_full(char *cmd, LsExecResult *out) {
    int stdout_pipe[2], stderr_pipe[2];
    pipe(stdout_pipe);
    pipe(stderr_pipe);

    pid_t pid = fork();
    if (pid == 0) {
        // 子进程
        close(stdout_pipe[0]); close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]); close(stderr_pipe[1]);
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        _exit(127);
    }

    // 父进程：关闭写端
    close(stdout_pipe[1]); close(stderr_pipe[1]);

    // 同时读两个管道（防止子进程因管道缓冲区满而阻塞）
    // 用 select/poll 或两个线程读；简单实现用顺序读（足够脚本场景）
    out->stdout = read_fd_to_lsstring(stdout_pipe[0]);
    out->stderr = read_fd_to_lsstring(stderr_pipe[0]);

    int status;
    waitpid(pid, &status, 0);
    out->exit_code = WEXITSTATUS(status);
    out->ok = 1;
}
```

> **注意**：顺序读两个管道存在死锁风险（子进程写 stderr 满 → 阻塞，但父进程在读 stdout）。
> 脚本场景（输出通常 < 64 KB）基本安全；大输出场景建议 v2 改 `select`/线程。

#### Windows 实现

```c
// runtime/proc_helpers.c — Windows 路径
void __ls_proc_exec_full(char *cmd, LsExecResult *out) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE stdout_r, stdout_w, stderr_r, stderr_w;
    CreatePipe(&stdout_r, &stdout_w, &sa, 0);
    CreatePipe(&stderr_r, &stderr_w, &sa, 0);
    // 读端设为不可继承
    SetHandleInformation(stdout_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags     = STARTF_USESTDHANDLES;
    si.hStdOutput  = stdout_w;
    si.hStdError   = stderr_w;
    si.hStdInput   = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL,
                             TRUE, 0, NULL, NULL, &si, &pi);
    CloseHandle(stdout_w); CloseHandle(stderr_w);

    if (!ok) { out->ok = 0; return; }

    out->stdout = read_handle_to_lsstring(stdout_r);
    out->stderr = read_handle_to_lsstring(stderr_r);
    CloseHandle(stdout_r); CloseHandle(stderr_r);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code;
    GetExitCodeProcess(pi.hProcess, &code);
    out->exit_code = (int)code;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    out->ok = 1;
}
```

#### C 侧结构体（`runtime/proc_helpers.h`）

```c
typedef struct {
    LsString stdout;
    LsString stderr;
    int      exit_code;
    int      ok;          // 0 = 进程无法启动
} LsExecResult;
```

Codegen 端：
- 在栈上 alloca `LsExecResult`，call `@__ls_proc_exec_full`
- 检查 `ok` 字段：0 → `Result Err("failed to start process")`
- 1 → 把 stdout/stderr/exit_code 填入 LS `ExecResult` struct，返回 `Result Ok`

---

## 7. `proc.pid()` / `proc.exit()`

直接 emit libc 调用，无 helper：

```c
// proc.pid()
#ifdef _WIN32
    emit: call @GetCurrentProcessId() → i32
#else
    emit: call @getpid() → i32
#endif

// proc.exit(code)
    emit: call @exit(code)
    emit: unreachable
```

---

## 8. JIT 符号注册

`jit_init` 中需注册以下新符号：

```c
// runtime/proc_helpers.c 中的 helper
{ "__ls_proc_exec",      &__ls_proc_exec      },
{ "__ls_proc_exec_full", &__ls_proc_exec_full },
{ "__ls_env_all",        &__ls_env_all        },

// main.c 中的全局变量
{ "__ls_main_argc",      &ls_main_argc        },
{ "__ls_main_argv",      &ls_main_argv        },
```

平台相关 libc 函数（`getenv`, `setenv`, `system`, `popen` 等）通过
LLJIT 的 process symbol resolver 自动解析，无需手动注册。

---

## 9. CMakeLists 改动

```cmake
# 新增 proc_helpers 编译单元
target_sources(ls PRIVATE
    runtime/proc_helpers.c
    src/builtins_proc.c
    src/builtins_proc_cg.c
    src/builtins_env.c
    src/builtins_env_cg.c
)

# Windows：链接 kernel32（GetCurrentProcessId / CreateProcess）
if(WIN32)
    target_link_libraries(ls PRIVATE kernel32)
endif()
```

`kernel32` 在 Windows 上通常已隐式链接，保险起见显式声明。

---

## 10. 模块注册（`src/module.c`）

仿照 `math` / `io` 的注册路径：

```c
// module_load 中
if (strcmp(name, "proc") == 0 && !user_file_exists("proc")) {
    return builtin_proc_make_module(checker);
}
if (strcmp(name, "env") == 0 && !user_file_exists("env")) {
    return builtin_env_make_module(checker);
}
```

用户可用同名 `.ls` 文件完全 shadow 内建模块。

---

## 11. 类型注册（`src/builtins_proc.c`）

```c
// ExecResult struct 注册
Type *exec_result_type = type_struct("ExecResult");
type_struct_add_field(exec_result_type, "stdout",    type_string());
type_struct_add_field(exec_result_type, "stderr",    type_string());
type_struct_add_field(exec_result_type, "exit_code", type_int());
exec_result_type->as.strukt.has_drop = true;
checker_register_struct(checker, "ExecResult", exec_result_type);

// Result(ExecResult, string) 模板实例化
Type *result_exec = checker_instantiate_result(checker,
                        exec_result_type, type_string());

// proc 模块导出的函数类型
Type *proc_module = type_module("proc");
type_module_add(proc_module, "args",    type_fn(NULL, 0, type_vec(type_string())));
type_module_add(proc_module, "program", type_fn(NULL, 0, type_string()));
type_module_add(proc_module, "pid",     type_fn(NULL, 0, type_int()));
type_module_add(proc_module, "exit",    type_fn1(type_int(), type_void()));
type_module_add(proc_module, "exec",    type_fn1(type_string(), result_str_str));
type_module_add(proc_module, "run",     type_fn1(type_string(), type_int()));
type_module_add(proc_module, "exec_full", type_fn1(type_string(), result_exec));
```

---

## 12. memcheck 集成

`runtime/proc_helpers.c` 内的 malloc/realloc 走 `ls_mc_alloc`/`ls_mc_realloc`（与现有 builtins 一致）：

```c
// proc_helpers.c 内用 LS memcheck 宏
#ifdef LS_MEMCHECK
#  include "memcheck.h"
#  define PROC_MALLOC(s)     ls_mc_alloc(s, &proc_site)
#  define PROC_REALLOC(p,s)  ls_mc_realloc(p, s, &proc_site)
#  define PROC_FREE(p)       ls_mc_free(p)
#else
#  define PROC_MALLOC(s)     malloc(s)
#  define PROC_REALLOC(p,s)  realloc(p,s)
#  define PROC_FREE(p)       free(p)
#endif
```

kind 标签：`proc.exec.stdout` / `proc.exec.stderr` / `proc.exec_full.stdout` / `proc.exec_full.stderr`

---

## 13. 测试计划

### 端到端测试（`tests/samples/`）

| 文件 | 覆盖 |
|------|------|
| `env_basic_test.ls` | get/get_or/require/set/delete/has/all |
| `proc_args_test.ls` | args()/program() — 需要 CLI 参数传入 |
| `proc_exec_test.ls` | exec/run/exec_full — git/echo 命令 |

### ctest 注册（`tests/test_proc_env.cmake`）

```cmake
# env 基础测试（AOT + JIT）
add_test_aot(test_env_basic   tests/samples/env_basic_test.ls)
add_test_jit(test_env_basic_jit tests/samples/env_basic_test.ls)

# proc exec 测试（AOT + JIT + memcheck）
add_test_aot(test_proc_exec   tests/samples/proc_exec_test.ls)
add_test_jit_memcheck(test_proc_exec_mc tests/samples/proc_exec_test.ls)
```

---

## 14. 实现顺序（建议）

```
Step 1  env 模块（get/get_or/require/set/delete/has）
        — 最简单，全部 libc，无 C helper，先跑通模块框架

Step 2  env.all()
        — 加 __ls_env_all C helper，验证 Windows/Linux 路径

Step 3  proc.args() / proc.program() / proc.pid() / proc.exit()
        — 全局 argc/argv 存储 + JIT 符号注册

Step 4  proc.run()
        — system() 直调，WEXITSTATUS 处理

Step 5  proc.exec()
        — popen + 动态缓冲区 + __ls_proc_exec helper

Step 6  proc.exec_full()
        — CreateProcess / fork+exec，两平台测试

Step 7  memcheck 集成 + ctest 注册
```

---

## 15. 已知限制（v1）

| 限制 | 说明 | 未来版本 |
|------|------|---------|
| `exec`/`run` 使用 shell 解析 | `popen`/`system` 走 `/bin/sh` 或 `cmd.exe`，存在注入风险 | v2 加 `proc.exec_argv(vec(string))` 绕过 shell |
| `exec_full` 顺序读管道 | 大输出（>64KB）可能死锁 | v2 改 `select`/线程读 |
| stdin 不归 `proc` | `proc.read_line()` 不存在 | 归 `io.read_line`（已规划 v3） |
| 无后台执行 | 无 `proc.spawn_bg` | 归 `thread` 模块 |
| 无信号处理 | 无 `proc.kill` | 归 `thread` 模块 |
| `exec_full` 命令字符串解析 | 同 `exec`，依赖 shell | v2 加 argv 变体 |
