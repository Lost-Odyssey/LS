# LS (LLVM Script) — Claude Code 项目上下文

## 0. 重要交互规则

- **永远用中文回答用户**，无论用户用什么语言提问。

---

## 1. 项目概述

用 C 语言实现通用编程语言 **LS** (LLVM Script)，C + Ruby 混合风格，LLVM 18 后端，支持 AOT 编译和 JIT 增量编译。

### 1.1 核心设计决策（不可更改）

| 维度 | 决策 |
|------|------|
| 实现语言 | C17（MSVC on Windows，兼容 GCC/Clang） |
| 构建系统 | CMake >= 3.20 + Visual Studio 17 2022 (或 Ninja) |
| 目标平台 | Windows 10 x64（首要），Linux/macOS（次要） |
| 后端 | LLVM 18 C API |
| LLVM 链接 | **静态链接**（ls.exe 独立可执行，不依赖 LLVM DLL） |
| 类型系统 | 静态类型 + 显式标注 |
| 内存管理 | 手动 malloc/free，无 GC |
| JIT 引擎 | LLJIT（后期迁移 ORC v2） |
| C FFI | 动态加载 shared library（Windows: LoadLibrary/GetProcAddress） |
| 外部依赖 | 仅 LLVM |

### 1.2 已实现语言特性

- 变量：C 风格类型前置 `int x = 42`，分号**可选**
- 函数：`fn add(int a, int b) -> int { ... }`，无返回值省略 `-> type`
- `struct` + `impl` 块（旧式隐式 `self` / 显式 `&self` / `&!self`，`static fn` 静态方法）
- `match` 模式匹配（enum 变体解构 + OR-pattern `1 | 2 =>` + 强制穷尽性检查）
- `enum`（tagged union）：payload 变体、自递归 box、has_drop 自动 drop；`Option(T)` / `Result(T,E)` 模板
- `try` 早返操作符（Zig 风格）
- `for (init; cond; update)` / `for i in 0..10` range 迭代
- `string`（RAII）：20+ 方法；借用 `&string` / `&!string`
- `array(T,N)`、`vec(T)`（含字面量 `[..]`）、`map(K,V)`
- `Block(...)->R` 闭包类型；`|x| body` 字面量；trailing closure 糖；完整捕获语义（POD/string/struct/vec/map/enum/Block）
- `print()`、`f"text {expr}"` 格式化字符串
- `module` / `import` / `import X as Y` 模块系统
- C FFI：`lib x = load("foo.dll")` + `extern fn` + `lib.call(...)`
- 全局变量；`object` 类型擦除指针
- 内建 stdlib：`math`（LLVM intrinsic）、`io`（v1/v2）、`std.time`、`std.json`
- 数值隐式扩展（Zig 风，`int→i64/f64` 等）
- `ls run --memcheck`：内存检查器（leak/dfree/ifree + backtrace + verbose/strict 模式）

> 完整语法示例见 [docs/syntax_guide.md](docs/syntax_guide.md)

---

## 2. 目录结构

```
src/
  main.c          入口：CLI、REPL、文件编译
  common.h        公共类型、宏（含 CG_DEBUG 开关）
  scanner.h/c     词法分析
  token.h         Token 枚举 + Token 结构体
  ast.h/c         AST 节点定义
  parser.h/c      Pratt Parser
  types.h/c       类型系统 + 类型检查器
  symtable.h      符号表（作用域链）
  codegen.h/c     AST → LLVM IR
  jit.h/c         LLJIT 封装（增量编译）
  ffi.h/c         C FFI：动态库加载
  module.h/c      模块系统

runtime/builtins.c    内建函数运行时
runtime/memcheck.c    内存检查器
runtime/os_win32.c    平台 C 后端（time/sleep/...）
tests/test_*.c        单元测试
tests/samples/*.ls    端到端测试
std/                  纯 LS 标准库（json.ls, time.ls, ...）
docs/                 设计文档
```

---

## 3. 构建与运行

```powershell
# Windows (Visual Studio，推荐)
cmake -B build -G "Visual Studio 17 2022" -A x64 -DLLVM_DIR="C:\llvm\lib\cmake\llvm"
cmake --build build --config Release
```

```
# 使用
ls.exe compile input.ls -o output.exe   # AOT 编译
ls.exe run input.ls                     # JIT 执行
ls.exe run --memcheck input.ls          # JIT + 内存检查
ls.exe repl                             # REPL

# 测试（推荐：--repeat until-pass:2 自动重试 flaky 测试）
cd build && ctest --output-on-failure -C Release --repeat until-pass:2
```

> ⚠️ **测试 flake 说明**：AOT 测试会 `compile` 出 `.exe` 后立刻运行/删除；Windows
> Defender 实时扫描会瞬时锁住刚落盘的 `.exe`，导致**间歇性**编译/运行/删除失败
> （现象：全量跑随机某个 AOT 测试 fail，单独重跑即过）。**与代码逻辑无关、ctest
> 默认串行（非并行导致）**。对策：全量跑统一加 `--repeat until-pass:2`，flake 自愈
> 而真回归（连续失败）仍会暴露。治本可把 `build/` 加入 Defender 排除项。

> 完整 CMakeLists.txt 配置见 [docs/build.md](docs/build.md)

---

## 4. 实现阶段状态

| Phase | 内容 | 状态 |
|-------|------|------|
| 1 | Scanner + Token | ✅ |
| 2 | AST + Pratt Parser | ✅ |
| 3 | 类型系统 + 符号表 | ✅ |
| 4 | LLVM IR 代码生成 (AOT) | ✅ |
| 5 | LLJIT 增量编译 + REPL | ✅ |
| 6 | C FFI（动态库加载） | ✅ |
| 7 | 模块系统（module/import） | ✅ |
| 8 | enum + Option/Result + match 穷尽性 | ✅ |
| 8.5 | `try` 早返操作符 | ✅ |
| 9 | math / io v1+v2 / memcheck A~D / std.time | ✅ |
| 10 | 闭包 Phase A~F（完整捕获 + drop + 移动语义） | ✅ |
| 11 | std.json（纯 LS，递归下降 parser + stringify） | ✅ |
| — | match OR-pattern + 整数 switch（bugs/18） | ✅ |
| — | L-009 跨模块函数名 mangling（`<modpath>__<fn>`） | ✅ |
| — | BF-040 array 元素字段读取 double-drop 修复 | ✅ |
| — | 模块全局变量命名空间（P1-1~P1-4）+ B-1 同名类型冲突检测 | ✅ |
| — | 操作符重载（Add/Sub/Mul/Div/Rem/Eq/Ord，Ruby 风 `fn +`，checker 降级） | ✅ |
| — | REPL 改进：自研行编辑器 + 通用多行 + import 持久化修复（语法高亮暂关；L-010 限制见下） | ✅ |
| — | 容器 drop 修复：struct 含 vec/enum 字段自动 drop（L-011a）+ 嵌套 `vec(vec(...))` drop/clone（L-011b）+ vec rvalue 临时实参 drop（E） | ✅ |
| — | std.md Markdown 模块 Phase A（写：builder + render + fmt，纯 LS，扁平 list/table 规避 L-011c） | ✅ |

**当前测试**：ctest 91/91（新增 std.md `test_std_md_jit`；REPL `test_repl`/`test_repl_import`；操作符重载）

> ⚠️ **REPL 已知限制 L-010**：`ls repl` 中跨多条输入行对同一类 has_drop enum/struct 值（如 `import std.json` 的 `JsonValue`）反复调用会析构的函数（`stringify` 等）→ 段错误。`ls run` 跑 `.ls` 文件不受影响。根因：每条 REPL snippet 是独立 JIT 模块，imported 模块 drop/clone 辅助被跨模块 strip 与 RAII 析构交互出错。修法方向：imported 模块在 REPL 只发射一次。详见 [docs/feature_inventory.md](docs/feature_inventory.md) 三、L-010。

> 各阶段实现细节见 [docs/features_history.md](docs/features_history.md)

---

## 5. 编码规范（必须遵守）

### 5.1 命名规范

- 函数：`module_verb_noun`（如 `scanner_next_token`, `codegen_emit_binary`）
- 结构体：`PascalCase`；枚举值：`UPPER_SNAKE_CASE`；宏：`UPPER_SNAKE_CASE`；局部变量：`snake_case`
- 指针星号靠近类型：`int *ptr`（不是 `int* ptr`）

### 5.2 内存管理

- 所有 `malloc` 必须检查返回值；谁分配谁释放
- AST 节点用 `ast_free` 递归释放；LLVM 对象用 `LLVMDispose*` 释放
- 新特性开发期间用 `ls run --memcheck` 验证 0 leaks / 0 dfree

### 5.3 错误处理

- Scanner 错误 → `TOKEN_ERROR`（含行号列号）
- Parser 错误 → `had_error = true`，同步到语句边界后继续
- 类型错误一次性收集（最多 20 条），格式：`[error_type] file:line:col: message`

### 5.4 测试要求

每个新特性必须有：单元测试（`test_*.c`）+ `.ls` 端到端测试（AOT + JIT + memcheck 三重验证）。

```c
#define ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); exit(1); } \
} while(0)
```

### 5.5 CG_DEBUG 内存跟踪（必须遵守）

`CG_DEBUG` 定义在 `src/common.h`（默认 `0`）。启用：`cmake ... -DLS_CG_DEBUG=ON`。

**规则**：
1. 每个新的自动内存分配/释放/clone 点必须加 `#if CG_DEBUG` 块
2. 使用 `cg_emit_debug_printf(ctx, fmt, args, nargs)` 接口
3. 不要在 `#if CG_DEBUG` 块外调用 `cg_emit_debug_printf`
4. `common.h` 是 `CG_DEBUG` 的唯一权威来源

---

## 6. 待实现特性

- 内建 `io` v3+：`read_line` / `flush` / 目录列举
- `File` 自动 drop（当前需手动 `io.close`）
- 内建 stdlib 扩展：`fs` / `string` / `env` 模块
- `vec.get` / `map.get` 改返回 `Option(T)`（breaking change）
- 用户自定义泛型（`struct LinkedList(T)`）
- 借用作为返回类型 / 变量声明 / struct 字段（需生命期系统）
- **Phase G**：Block env 深拷贝（`Block g = ns[i]` 当前被 checker 拒绝）→ [docs/block_clone_plan.md](docs/block_clone_plan.md)
- ~~**Phase H**：struct 深拷贝~~ ✅ 已完成（验证 2026-05-29）：`MyStruct b = vec_of_struct[i]` 对 has_drop struct 自动深拷贝，memcheck clean（含嵌套 struct + 函数返回 vec）
- ~~**L-009**：跨模块函数名 LLVM mangling~~ ✅ 已完成（2026-05-29）：模块自由函数符号前缀化 `<modpath>__<fn>`，消除同名崩溃/静默错值；根/主文件函数不变。
- **L-009.1**：跨模块同名 mangling 收尾 → [docs/plan_l009_mangling.md](docs/plan_l009_mangling.md) §6
  - ~~6.A 模块泛型~~ ✅ 已完成（2026-05-30）：修 A1（模块泛型实例化丢弃→连单模块都不可用）+ A2（同名不同体泛型跨模块静默错值），符号 `<mod>__fn(args)`
- ~~**模块命名空间收尾 Part 1**（全局变量）~~ ✅ 已完成（2026-05-30）：模块全局变量符号前缀化（`<mod>__var`）+ scope 注册 + has_drop 全局 cleanup；`mod.VAR` 跨模块访问；ctest 76/76。→ [docs/plan_module_namespace.md](docs/plan_module_namespace.md) Step P1-1~P1-4
- ~~**模块命名空间收尾 B-1**（B-safe struct/enum 冲突检测）~~ ✅ 已完成（2026-05-30）：同名 struct/enum 来自多个模块 → 清晰 checker 错误 "multiple imported modules"，消除 GEP 崩溃/静默损坏。ctest 76/76。→ [docs/plan_module_namespace.md](docs/plan_module_namespace.md) Step B-1
- ~~**模块命名空间收尾 B-2**（struct/enum LLVM 类型名前缀化）~~ ✅ 已完成（2026-05-30）：`Type.strukt/enom.llvm_name` 字段存前缀名，codegen 所有 LLVM 命名点用前缀名，checker 注册表仍用裸名（零破坏）。ctest 79/79。→ [docs/plan_module_namespace.md](docs/plan_module_namespace.md) Step B-2
- ~~**模块命名空间收尾 B-3**（impl 方法/drop/clone 名跟随前缀）~~ ✅ 已完成（2026-05-30）：`codegen_impl_decl`/`codegen_impl_trait_decl` 在 `current_emit_module != NULL` 时前缀化 struct 名，静态方法调用按 `llvm_name` 解析。ctest 79/79。→ [docs/plan_module_namespace.md](docs/plan_module_namespace.md) Step B-3
- **模块命名空间收尾 B-4~B-6**（B-full 真命名空间）待做：类型位置限定语法 `mod.Type`（B-4）+ enum 跨模块变体（B-5）+ 综合 memcheck（B-6）。→ [docs/plan_module_namespace.md](docs/plan_module_namespace.md) Step B-4~B-6
- 正则表达式 builtin；f16 半精度浮点

> 已完成特性的详细实现记录见 [docs/features_history.md](docs/features_history.md)

---

## 7. 所有权与 Move 语义（摘要）

**LsString 三态**：`cap==0` Static（不 free）/ `cap>0` Owned（退出作用域 free）/ `cap==-1` Moved（跳过 free）

**核心规则**：变量一旦在任何路径被 move → 视为死亡，不可再用。`LIVE → MAYBE_MOVED → MOVED`（单调不可逆）

**借用（ABI 要点）**：
- `&string` by-value（cap=0 标记）；其他 `&T` / `&!T` 全部 pointer
- 只读借用支持 auto-borrow；可写借用必须显式 `f(&!x)`
- 支持类型：`string / vec(T) / map(K,V) / struct`；仅用于函数参数位置

**Move 类型**：`string` / `struct(has_drop)` / `vec(T)` / `map(K,V)` / `Block(...)`

> 完整借用规则表、运行时保护、实现计划见 [docs/ownership.md](docs/ownership.md)

---

## 8. 闭包捕获策略（摘要）

| 捕获类型 | 策略 | outer 变量 |
|----------|------|------------|
| `int / f64 / bool / char / *T / object` | **by-copy** | 保持 live |
| `array(POD, N)` | **by-copy** | 保持 live（snapshot） |
| `string` | **by-move** | 标 MOVED（cap = −1） |
| `struct(has_drop)` | **by-move** | 标 MOVED（moved_flag） |
| `vec(T)` | **by-ref** | 保持 live，可继续 push |
| `map(K,V)` | **by-ref** | 保持 live，可继续 set |
| `has_drop enum` | **by-move** | 标 MOVED（moved_flag） |
| `[move v]` 显式 | **by-move** | 标 MOVED（vec/map 专用） |

⚠️ **by-ref 捕获的 vec/map 闭包不能 outlive 外层变量**（编译器不检查，用户自行保证）。工厂函数返回 vec/map 捕获的闭包会产生悬垂指针。string/struct 使用 by-move 正是为了避免这个问题。

> 详细设计、悬垂风险示例、未来演进路径见 [docs/closures_plan.md](docs/closures_plan.md)
