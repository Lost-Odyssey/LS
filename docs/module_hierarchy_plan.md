# LS 模块层级方案（Phase M）

> 状态：**设计中**，尚未实现

## 0. 动机

当前 stdlib 是扁平结构（`io.ls` / `proc.ls` / `env.ls` / `os.ls` 并列在 `stdlib/` 根目录），用户代码 `import io` 与用户自建 `io.ls` 存在 shadow 风险，且无法区分"标准库模块"和"用户模块"。

目标：

1. stdlib 全部挂在 `std` 命名空间下，`import std.io` 明确表示标准库
2. 引入 `std.c` 模块集中所有 C/CRT 绑定，其他 stdlib 文件零 `extern fn`
3. 保持调用语法简洁——`io.read_file()`（自动绑定末段名）
4. 提供 `as` 别名语法处理命名冲突

---

## 1. 目录结构

### 1.1 当前（扁平）

```
stdlib/
  os.ls          module os
  io.ls          module io
  proc.ls        module proc
  env.ls         module env
```

### 1.2 目标（层级）

```
stdlib/
  std/
    c.ls         module std.c       CRT + runtime helper 集中绑定层
    os.ls        module std.os      平台后端（ls_os_* extern）
    io.ls        module std.io      文件 I/O 高层 API
    proc.ls      module std.proc    进程管理
    env.ls       module std.env     环境变量
```

用户自建模块不放在 `std/` 下，保持在源文件同目录或自定义路径。

---

## 2. 语言语义变更

### 2.1 `module` 声明支持点分名

```ls
module std.io        // 当前仅支持 module io
```

**Parser 改动**：`parse_module_decl` 扩展为循环消费 `IDENT (.IDENT)*`，与 `parse_import_decl` 同构。

### 2.2 `import` 末段自动绑定（Go 风格）

```ls
import std.io        // scope 绑定 key = "io"（末段）
io.read_file("x")   // 两层访问，checker/codegen 零改动
```

| import 语句 | scope key | 调用方式 |
|-------------|-----------|----------|
| `import std.io` | `io` | `io.read_file()` |
| `import std.proc` | `proc` | `proc.exec()` |
| `import mylib.utils` | `utils` | `utils.helper()` |
| `import io` | `io` | `io.read_file()`（向后兼容扁平） |

**关键决策**：`scope_define` 使用**末段名**而非全路径。内部 `ModuleRegistry` 仍用全路径 `"std.io"` 作 key（避免不同路径指向同一末段时 registry 冲突）。

**冲突检测**：若同一作用域已有相同末段名（如 `import std.io` 和 `import mylib.io`），报编译错，要求使用 `as` 别名。

### 2.3 `as` 别名语法

```ls
import std.io as file_io
file_io.read_file("x")

import std.os as _os       // stdlib 内部用，避免与用户 os 冲突
```

**Parser**：`parse_import_decl` 末尾检测 `as` 关键字（新增 `TOKEN_AS`，或复用 identifier `"as"` 的 contextual keyword），读取别名 IDENT。

**AST**：`import_decl` 新增 `char *alias` 字段（NULL 表示无别名）。

**Checker**：
```c
const char *bind_name = decl->as.import_decl.alias
    ? decl->as.import_decl.alias
    : last_segment(import_path);    // "std.io" → "io"
scope_define(c->current_scope, bind_name, mod_type);
```

### 2.4 `module` 声明与 `import` 路径的一致性校验

Checker 在处理模块 AST 时，验证 `module_decl.name` 的**末段**与文件名匹配：

```
stdlib/std/io.ls  → module std.io  ✓
stdlib/std/io.ls  → module io      ✗ warning: module name 'io' does not match path 'std.io'
```

仅 warning，不 hard error（允许渐进迁移）。

---

## 3. `std.c` 模块设计

### 3.1 职责

集中声明所有 C 标准库和 LS runtime helper 的 `extern fn`，作为 stdlib 的唯一 C 绑定入口（类似 Zig 的 `std.c`）。

### 3.2 内容

```ls
// stdlib/std/c.ls
module std.c

// ---- C 标准库（CRT）----
extern {
    fn fopen(string path, string mode) -> object
    fn fclose(object fp) -> int
    fn fread(object buf, i64 sz, i64 n, object fp) -> i64
    fn fwrite(object buf, i64 sz, i64 n, object fp) -> i64
    fn strlen(string s) -> i64
    fn malloc(i64 size) -> object
    fn free(object ptr)
    fn system(string cmd) -> int
    fn strerror(int e) -> object
}

// ---- LS runtime helpers（builtins.c）----
extern fn __ls_get_argc() -> int
extern fn __ls_get_argv(int i) -> object
extern fn __ls_proc_exit(int code)
extern fn __ls_init_args(int argc, object argv)
```

### 3.3 各 stdlib 模块迁移

**std/io.ls**（改动最大）：
```ls
module std.io
import std.c as c
import std.os as _os

fn read_file(string path) -> Result(string, string) {
    object fp = c.fopen(path, "rb")
    ...
    _os.raw_fseek64(fp, 0, 2)
    i64 sz = _os.raw_ftell64(fp)
    ...
    object buf = c.malloc(sz + 1)
    i64 nread = c.fread(buf, 1, sz, fp)
    c.fclose(fp)
    ...
}
```

**std/proc.ls**：
```ls
module std.proc
import std.c as c
import std.os as _os

fn run(string cmd) -> int {
    int raw = c.system(cmd)
    return _os.raw_wait_exit_code(raw)
}

fn args() -> vec(string) {
    int n = c.__ls_get_argc()      // 或 c.get_argc() 若包装
    ...
}
```

**std/env.ls**：
```ls
module std.env
import std.os as _os
import std.c as c                  // 仅 __ls_proc_exit

fn require(string name) -> string {
    ...
    c.__ls_proc_exit(1)
    ...
}
```

**std/os.ls**（不变，仍是 ls_os_* 的唯一声明点）：
```ls
module std.os
extern fn ls_os_fseek64(object fp, i64 off, int origin) -> int
...
```

### 3.4 extern fn 分布终态

| 文件 | extern fn | 说明 |
|------|-----------|------|
| `std/c.ls` | fopen, fclose, fread, fwrite, strlen, malloc, free, system, strerror, __ls_get_argc, __ls_get_argv, __ls_proc_exit, __ls_init_args | CRT + runtime |
| `std/os.ls` | ls_os_* (全部 16 个) | 平台后端 |
| `std/io.ls` | 零 | 全部走 c.xxx / _os.raw_xxx |
| `std/proc.ls` | 零 | 全部走 c.xxx / _os.raw_xxx |
| `std/env.ls` | 零 | 全部走 _os.raw_xxx / c.xxx |

---

## 4. 实现计划

### Phase M.1 — Parser + AST（0.5 天）

**parser.c**：
- `parse_module_decl`：扩展为 `IDENT (.IDENT)*` 循环（与 `parse_import_decl` 对称）
- `parse_import_decl`：末尾增加 `as IDENT` 可选解析

**ast.h**：
- `import_decl` 新增 `char *alias` 字段
- `ast_free` 释放 `alias`

**token.h**（可选）：
- 若加 `TOKEN_AS` keyword，更新 keyword 表 + binary search
- 或用 contextual keyword（检测 `"as"` 文本），避免保留字冲突

### Phase M.2 — Checker 绑定逻辑（1 天）

**checker.c `AST_IMPORT_DECL` 处理**（约 5987 行）：
```c
// 旧：scope_define(c->current_scope, import_path, mod_type);
// 新：
const char *bind_name = decl->as.import_decl.alias;
if (!bind_name) {
    bind_name = last_segment(import_path);  // "std.io" → "io"
}
// 冲突检测
Symbol *existing = scope_resolve(c->current_scope, bind_name);
if (existing && existing->type->kind == TYPE_MODULE) {
    checker_error(c, ..., "conflicting import name '%s'; use 'as' to alias", bind_name);
    break;
}
scope_define(c->current_scope, bind_name, mod_type);
```

**builtin_module_exists / builtin_module_make_type**：
- `"math"` 改为同时接受 `"std.math"` 和 `"math"`（向后兼容期）
- 或只接受 `"math"`，让 `std.math` 走 stdlib 文件路径（如果 stdlib/std/math.ls 存在的话）

**module_user_file_exists**：无需改动，已支持点分路径。

### Phase M.3 — 目录迁移 + `std.c` 创建（1 天）

1. 创建 `stdlib/std/` 目录
2. 移动：`stdlib/os.ls` → `stdlib/std/os.ls`，更新 `module os` → `module std.os`
3. 同理迁移 io.ls / proc.ls / env.ls
4. 创建 `stdlib/std/c.ls`，迁入所有 CRT + runtime extern fn
5. 更新各模块 import 语句

### Phase M.4 — 向后兼容 + 测试（1 天）

**向后兼容策略**：

- `import io` 仍然能工作——`module_resolve_path` 先查用户同目录 `io.ls`，再查 `stdlib/io.ls`（不存在），最后查内建。为过渡期在 `stdlib/` 根目录放 shim 文件：

```ls
// stdlib/io.ls (shim, 过渡期保留)
// DEPRECATED: use 'import std.io' instead
module io
import std.io

// re-export all std.io symbols...
```

问题：LS 当前不支持 re-export 语义。更简单的方案是在 `module.c` 的 `resolve_stdlib_path` 里加一个 fallback：找不到 `stdlib/<name>.ls` 时尝试 `stdlib/std/<name>.ls`。

```c
// resolve_stdlib_path 末尾增加：
if (f == NULL) {
    // Fallback: try stdlib/std/<name>.ls for backwards compatibility
    snprintf(full, full_len, "%s%sstdlib%sstd%s%s.ls",
             root, SEP, SEP, SEP, rel_path);
    f = fopen(full, "rb");
}
```

这样 `import io` → 先查 `stdlib/io.ls`（不存在）→ 查 `stdlib/std/io.ls`（命中）。scope key 按末段绑定为 `"io"`，调用方式不变。

**测试**：
- 更新所有 `tests/samples/*test.ls` 中的 `import io` → `import std.io`（或依赖兼容 fallback）
- 新增 `tests/samples/module_hierarchy_test.ls`：验证 `import std.io` + `import std.proc` + `as` 别名
- 新增 `tests/samples/module_as_test.ls`：验证 `import std.io as f` + `f.read_file()`
- ctest 全通过

### Phase M.5 — Codegen 验证（0.5 天）

Codegen 层**大概率不需要改动**：

- `LLVMGetNamedFunction(ctx->module, fn_name)` 使用的是函数名（如 `"read_file"`），不含模块前缀
- 所有模块编译进同一个 LLVM module，函数名全局唯一（由 LS `module` 声明确保不冲突）
- 若未来不同层级模块有同名函数，需引入 name mangling（如 `std_io_read_file`），但当前 stdlib 无此冲突

如果存在同名函数冲突（如 `std.io.open` 和 `std.proc.open`），需要在 codegen 层做 mangling：

```c
// 方案：模块前缀 + 函数名
// std.io 的 open → "std.io.open" 或 "__std_io__open"
```

这是 Phase M.5 验证是否需要、如果需要则实现的内容。

---

## 5. 改动矩阵

| 文件 | Phase | 改动类型 | 预估行数 |
|------|-------|---------|---------|
| `src/parser.c` | M.1 | `parse_module_decl` 扩展 + `as` 语法 | +30 |
| `src/ast.h` | M.1 | `import_decl.alias` 字段 | +3 |
| `src/ast.c` | M.1 | `ast_free` 释放 alias | +2 |
| `src/token.h` | M.1 | `TOKEN_AS` keyword（可选） | +2 |
| `src/checker.c` | M.2 | `last_segment` + 绑定逻辑 + 冲突检测 | +25 |
| `src/module.c` | M.4 | `resolve_stdlib_path` fallback | +10 |
| `stdlib/std/c.ls` | M.3 | 新建 | +25 |
| `stdlib/std/os.ls` | M.3 | 从 `stdlib/os.ls` 迁移，改 module 声明 | ~0（重命名） |
| `stdlib/std/io.ls` | M.3 | 改 module/import 声明，extern 迁入 c.ls | -10 |
| `stdlib/std/proc.ls` | M.3 | 改 module/import 声明，extern 迁入 c.ls | -10 |
| `stdlib/std/env.ls` | M.3 | 改 module/import 声明，extern 迁入 c.ls | -5 |
| `src/codegen.c` | M.5 | name mangling（仅在同名冲突时） | 0~+40 |
| `tests/` | M.4 | 更新 import 语句 / 新增层级测试 | +50 |
| **总计** | | | **~130 行新代码 + 迁移** |

---

## 6. 风险与决策点

| 风险 | 缓解 |
|------|------|
| 函数名全局冲突（不同模块同名函数） | Phase M.5 按需引入 mangling；当前 stdlib 不冲突 |
| 向后兼容 `import io` 断裂 | `resolve_stdlib_path` fallback 到 `std/` 子目录 |
| `as` keyword 与用户变量名冲突 | 用 contextual keyword（仅 import 上下文识别），不加入保留字表 |
| `std.c` 中 `malloc`/`free` 与 memcheck wrapper 冲突 | memcheck 的 `cg_install_memcheck_wrappers` 在 LLVM IR 层重命名，与 LS 声明层无关 |
| 跨模块同名 extern fn（如 io.ls 和 proc.ls 都声明 fopen） | 集中到 std.c 后不再有此问题 |
| 内建 math 模块的 builtin 特殊路径 | 保持 `import math` 走内建，`import std.math` 走 stdlib（未来可选迁移） |

---

## 7. 里程碑

| 里程碑 | 内容 | 验收标准 |
|--------|------|---------|
| M.1 完成 | parser 支持 `module a.b` + `import a.b as c` | 解析测试通过 |
| M.2 完成 | checker 末段绑定 + 冲突检测 | `import std.io` → `io.read_file()` 类型检查通过 |
| M.3 完成 | 目录迁移 + std.c 创建 | 所有 stdlib 文件零 extern fn（除 std.c 和 std.os） |
| M.4 完成 | 向后兼容 + 测试全通过 | ctest 31/31 + 新增 module hierarchy 测试 |
| M.5 完成 | codegen 验证 / mangling（如需） | JIT + AOT 双路径全 PASS |

**总工作量预估：3-4 天**
